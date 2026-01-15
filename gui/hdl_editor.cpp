#include "hdl_editor.h"
#include <QPainter>
#include <QTextBlock>

// ============================================================================
// SYNTAX HIGHLIGHTER IMPLEMENTATION
// ============================================================================

HDLSyntaxHighlighter::HDLSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) 
{
    // Default formats
    keywordFormat.setForeground(QColor("#C586C0")); // Purple
    keywordFormat.setFontWeight(QFont::Bold);

    classFormat.setForeground(QColor("#4EC9B0"));   // Teal
    
    commentFormat.setForeground(QColor("#6A9955")); // Green
    commentFormat.setFontItalic(true);
    
    quotationFormat.setForeground(QColor("#CE9178")); // Orange
    
    functionFormat.setForeground(QColor("#DCDCAA"));  // Yellow
    
    numberFormat.setForeground(QColor("#B5CEA8"));    // Light Green
    
    setLanguage(VERILOG);
}

void HDLSyntaxHighlighter::setLanguage(Language lang) {
    currentLang = lang;
    setupRules();
    rehighlight();
}

void HDLSyntaxHighlighter::setupRules() {
    highlightingRules.clear();
    
    // Keywords based on language
    QStringList keywords;
    if (currentLang == VERILOG || currentLang == VERILOG_AMS) {
        keywords << "\\bmodule\\b" << "\\bendmodule\\b" << "\\binput\\b" << "\\boutput\\b"
                 << "\\binout\\b" << "\\bwire\\b" << "\\breg\\b" << "\\bassign\\b"
                 << "\\balways\\b" << "\\bbegin\\b" << "\\bend\\b" << "\\bif\\b"
                 << "\\belse\\b" << "\\bcase\\b" << "\\bendcase\\b" << "\\bposedge\\b"
                 << "\\bnegedge\\b" << "\\binitial\\b" << "\\bparameter\\b";
                 
        if (currentLang == VERILOG_AMS) {
            keywords << "\\banalog\\b" << "\\belectrical\\b" << "\\bground\\b" 
                     << "\\bresistor\\b" << "\\bcapacitor\\b" << "\\bvoltage\\b" 
                     << "\\bcurrent\\b" << "\\bddt\\b" << "\\bidt\\b";
        }
    } else if (currentLang == VHDL) {
        keywords << "\\bentity\\b" << "\\bis\\b" << "\\bport\\b" << "\\bmap\\b"
                 << "\\bend\\b" << "\\barchitecture\\b" << "\\bof\\b" << "\\bbegin\\b"
                 << "\\bprocess\\b" << "\\bif\\b" << "\\bthen\\b" << "\\belse\\b";
    }
    
    for (const QString &pattern : keywords) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }
    
    // Numbers
    HighlightingRule numRule;
    numRule.pattern = QRegularExpression("\\b\\d+'[bdh][0-9a-fA-F]+\\b|\\b\\d+\\b");
    numRule.format = numberFormat;
    highlightingRules.append(numRule);
    
    // Strings
    HighlightingRule quoteRule;
    quoteRule.pattern = QRegularExpression("\".*\"");
    quoteRule.format = quotationFormat;
    highlightingRules.append(quoteRule);
    
    // Comments
    HighlightingRule singleLineComment;
    singleLineComment.pattern = QRegularExpression("//[^\n]*");
    singleLineComment.format = commentFormat;
    highlightingRules.append(singleLineComment);
    
    // TODO: Multi-line comments
}

void HDLSyntaxHighlighter::highlightBlock(const QString &text) {
    for (const HighlightingRule &rule : highlightingRules) {
        QRegularExpressionMatchIterator i = rule.pattern.globalMatch(text);
        while (i.hasNext()) {
            QRegularExpressionMatch match = i.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}

// ============================================================================
// EDITOR IMPLEMENTATION
// ============================================================================

HDLEditor::HDLEditor(QWidget *parent) : QPlainTextEdit(parent) {
    lineNumberArea = new LineNumberArea(this);
    highlighter = new HDLSyntaxHighlighter(document());

    connect(this, &HDLEditor::blockCountChanged, this, &HDLEditor::updateLineNumberAreaWidth);
    connect(this, &HDLEditor::updateRequest, this, &HDLEditor::updateLineNumberArea);
    connect(this, &HDLEditor::cursorPositionChanged, this, &HDLEditor::highlightCurrentLine);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    
    // Style
    QFont font("Monaco"); // or Consolas, Menlo
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(12);
    setFont(font);
    
    QPalette p = palette();
    p.setColor(QPalette::Base, COL_BG);
    p.setColor(QPalette::Text, COL_FG);
    setPalette(p);
    
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    
    // Default Text
    setPlainText("// Verilog-AMS Design\nmodule amplifier(input vin, output vout);\n  electrical vin, vout;\n  parameter real gain = 10.0;\n\n  analog begin\n    V(vout) <+ gain * V(vin);\n  end\nendmodule");
}

void HDLEditor::setLanguage(HDLSyntaxHighlighter::Language lang) {
    highlighter->setLanguage(lang);
}

int HDLEditor::lineNumberAreaWidth() {
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    int space = 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void HDLEditor::updateLineNumberAreaWidth(int /* newBlockCount */) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void HDLEditor::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy)
        lineNumberArea->scroll(0, dy);
    else
        lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void HDLEditor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void HDLEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extraSelections;

    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(COL_CURRENT_LINE);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        extraSelections.append(selection);
    }

    setExtraSelections(extraSelections);
}

void HDLEditor::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), COL_BG.darker(110));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(COL_LINE_NUM);
            painter.drawText(0, top, lineNumberArea->width() - 5, fontMetrics().height(),
                             Qt::AlignRight, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
}
