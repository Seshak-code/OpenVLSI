#include "code_editor.h"
#include <QPainter>
#include <QTextBlock>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QMenu>
#include <QAction>
#include "gui/orchestration/worker_manager.h"

using namespace acutesim::gui::editors;

// ============================================================================
// Code Syntax Highlighter
// ============================================================================

CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) 
{
    // Define Formats (VS Code Dark Style)
    keywordFormat.setForeground(QColor("#569CD6")); // Blue
    keywordFormat.setFontWeight(QFont::Bold);
    
    classFormat.setForeground(QColor("#4EC9B0")); // Teal
    classFormat.setFontWeight(QFont::Bold);
    
    commentFormat.setForeground(QColor("#6A9955")); // Green
    
    quotationFormat.setForeground(QColor("#CE9178")); // Orange/Red
    
    functionFormat.setForeground(QColor("#DCDCAA")); // Yellow
    
    numberFormat.setForeground(QColor("#B5CEA8")); // Light Green

    // Default to plain text
    setLanguage(PLAIN_TEXT);
}

void CodeSyntaxHighlighter::setLanguage(Language lang) {
    currentLang = lang;
    setupRules();
    rehighlight();
}

void CodeSyntaxHighlighter::setupRules() {
    highlightingRules.clear();
    
    if (currentLang == PLAIN_TEXT) return;

    // Common Rules (Strings, Numbers)
    HighlightingRule rule;
    
    // Strings
    rule.pattern = QRegularExpression("\".*\"");
    rule.format = quotationFormat;
    highlightingRules.append(rule);
    
    // Numbers
    rule.pattern = QRegularExpression("\\b\\d+\\b");
    rule.format = numberFormat;
    highlightingRules.append(rule);

    // Language Specifics
    if (currentLang == VERILOG || currentLang == VERILOG_AMS) {
        QStringList keywords;
        keywords << "\\bmodule\\b" << "\\bendmodule\\b" << "\\binput\\b" << "\\boutput\\b"
                 << "\\bwire\\b" << "\\breg\\b" << "\\bassign\\b" << "\\balways\\b"
                 << "\\bbegin\\b" << "\\bend\\b" << "\\bif\\b" << "\\belse\\b"
                 << "\\bparameter\\b" << "\\binitial\\b";
                 
        if (currentLang == VERILOG_AMS) {
            keywords << "\\banalog\\b" << "\\belectrical\\b" << "\\bground\\b";
        }

        for (const QString &pattern : keywords) {
            rule.pattern = QRegularExpression(pattern);
            rule.format = keywordFormat;
            highlightingRules.append(rule);
        }
        
        // Comments
        rule.pattern = QRegularExpression("//[^\n]*");
        rule.format = commentFormat;
        highlightingRules.append(rule);
    }
    // ... Add Python/TCL support here later
}

void CodeSyntaxHighlighter::highlightBlock(const QString &text) {
    for (const HighlightingRule &rule : highlightingRules) {
        QRegularExpressionMatchIterator i = rule.pattern.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

// ============================================================================
// Code Editor Widget
// ============================================================================

CodeEditor::CodeEditor(const QString& filePath, QWidget *parent)
    : core::IEditor(parent), filePath_(filePath) 
{
    // Layout
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Text Edit
    textEdit_ = new InternalCodeEditor(this);
    textEdit_->setStyleSheet(QString("background-color: %1; color: %2; border: none;")
                             .arg(COL_BG.name(), COL_FG.name()));
    
    // Font
    QFont font("Monaco", 12); // Fallback
    font.setStyleHint(QFont::Monospace);
    textEdit_->setFont(font);

    // Line Numbers
    lineNumberArea = new LineNumberArea(this);

    layout->addWidget(lineNumberArea);
    layout->addWidget(textEdit_);

    // Connects
    connect(textEdit_, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(textEdit_, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(textEdit_, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(textEdit_, &QPlainTextEdit::textChanged, this, &CodeEditor::onTextUtilsChanged);

    // Highlighter
    highlighter = new CodeSyntaxHighlighter(textEdit_->document());

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    
    // Set language based on extension
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    if (ext == "v" || ext == "sv") setLanguage(CodeSyntaxHighlighter::VERILOG);
    else if (ext == "va" || ext == "vams") setLanguage(CodeSyntaxHighlighter::VERILOG_AMS);
    else if (ext == "py") setLanguage(CodeSyntaxHighlighter::PYTHON);
    else if (ext == "tcl") setLanguage(CodeSyntaxHighlighter::TCL);
    else setLanguage(CodeSyntaxHighlighter::PLAIN_TEXT);

    // Context Menu
    textEdit_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(textEdit_, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = textEdit_->createStandardContextMenu();
        
        QFileInfo fi(filePath_);
        QString ext = fi.suffix().toLower();
        if (ext == "yaml" || ext == "yml" || ext == "va") {
            menu->addSeparator();
            QAction* compileAction = menu->addAction("Compile Model (Physics)");
            connect(compileAction, &QAction::triggered, this, [this]() {
                acutesim::gui::orchestration::WorkerManager::instance()->submitPhysicsCompilation(filePath_);
            });
        }
        
        menu->exec(textEdit_->mapToGlobal(pos));
        delete menu;
    });
}

void CodeEditor::setLanguage(CodeSyntaxHighlighter::Language lang) {
    highlighter->setLanguage(lang);
}

QString CodeEditor::title() const {
    return QFileInfo(filePath_).fileName();
}



bool CodeEditor::isDirty() const {
    return isDirty_;
}

acutesim::gui::core::EditorCapabilities CodeEditor::capabilities() const {
    using namespace acutesim::gui::core;
    EditorCapabilities caps;
    QFileInfo fi(filePath_);
    QString ext = fi.suffix().toLower();

    if (ext == "v" || ext == "sv" || ext == "va" || ext == "vams") {
        caps.canSimulate = true; // HDL Simulation
        caps.canSynthesize = true; // Yosys
    } else if (ext == "py" || ext == "tcl") {
        caps.canRunScript = true;
    }
    return caps;
}

void CodeEditor::onTextUtilsChanged() {
    if (!isDirty_) {
        isDirty_ = true;
        emit contentChanged();
    }
}

int CodeEditor::lineNumberAreaWidth() {
    int digits = 1;
    int max = qMax(1, textEdit_->blockCount());
    while (max >= 10) {
        max /= 10;
        digits++;
    }
    int space = 10 + textEdit_->fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void CodeEditor::updateLineNumberAreaWidth(int /* newBlockCount */) {
    // lineNumberArea is managed by layout, but we define sizeHint
    // We might need to force update geometry
    // textEdit_->setViewportMargins(0, 0, 0, 0); // Removed as it uses HBox
    // So we just set its fixed width?
    lineNumberArea->setFixedWidth(lineNumberAreaWidth());
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) lineNumberArea->scroll(0, dy);
    else lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
    
    if (rect.contains(textEdit_->viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), COL_BG.darker(110));

    QTextBlock block = textEdit_->firstVisibleBlockPublic();
    int blockNumber = block.blockNumber();
    int top = (int) textEdit_->blockBoundingGeometryPublic(block).translated(textEdit_->contentOffsetPublic()).top();
    int bottom = top + (int) textEdit_->blockBoundingRectPublic(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(COL_LINE_NUM);
            painter.drawText(0, top, lineNumberArea->width() - 5, textEdit_->fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) textEdit_->blockBoundingRectPublic(block).height();
        blockNumber++;
    }
}

void CodeEditor::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    // Layout handles resizing
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!textEdit_->isReadOnly()) {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = COL_CURRENT_LINE;

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textEdit_->textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    textEdit_->setExtraSelections(extraSelections);
}
