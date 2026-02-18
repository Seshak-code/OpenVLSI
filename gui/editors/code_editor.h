#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include "gui/core/editor_manager.h" // For IEditor

// ============================================================================
// Code Syntax Highlighter
// ============================================================================

class CodeSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum Language { PLAIN_TEXT, VERILOG, VHDL, VERILOG_AMS, PYTHON, TCL };
    
    explicit CodeSyntaxHighlighter(QTextDocument *parent = nullptr);
    void setLanguage(Language lang);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;

    QTextCharFormat keywordFormat;
    QTextCharFormat classFormat;
    QTextCharFormat commentFormat;
    QTextCharFormat quotationFormat;
    QTextCharFormat functionFormat;
    QTextCharFormat numberFormat;
    
    Language currentLang;
    void setupRules();
};

// Internal editor logic exposing protected methods
class InternalCodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;
    
    // Wrappers for protected methods needed by LineNumberArea/CodeEditor
    QRectF blockBoundingGeometryPublic(const QTextBlock &block) const { 
        return blockBoundingGeometry(block); 
    }
    QRectF blockBoundingRectPublic(const QTextBlock &block) const { 
        return blockBoundingRect(block); 
    }
    QPointF contentOffsetPublic() const { 
        return contentOffset(); 
    }
    QTextBlock firstVisibleBlockPublic() const { 
        return firstVisibleBlock(); 
    }
    void setViewportMarginsPublic(int left, int top, int right, int bottom) {
        setViewportMargins(left, top, right, bottom);
    }
};

namespace acutesim::gui::editors {

class CodeEditor : public core::IEditor {
    Q_OBJECT
public:
    explicit CodeEditor(const QString& filePath, QWidget *parent = nullptr);

    // IEditor Interface Implementation
    QString filePath() const override { return filePath_; }
    QString title() const override;
    bool isDirty() const override;
    core::EditorCapabilities capabilities() const override;

    // Specific Methods
    void setLanguage(CodeSyntaxHighlighter::Language lang);
    void loadContent();                         // Load file from filePath_
    void setContent(const QString& text);       // Programmatic content set
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect, int dy);
    void onTextUtilsChanged();

private:
    QString filePath_;
    bool isDirty_ = false;
    
    InternalCodeEditor* textEdit_; // Using internal subclass
    QWidget *lineNumberArea;
    CodeSyntaxHighlighter *highlighter;
    
    const QColor COL_BG = QColor(30, 30, 30);
    const QColor COL_FG = QColor(220, 220, 220);
    const QColor COL_LINE_NUM = QColor(100, 100, 100);
    const QColor COL_CURRENT_LINE = QColor(40, 40, 40);
};

// Helper widget for line numbers
class LineNumberArea : public QWidget {
public:
    LineNumberArea(CodeEditor *editor) : QWidget(reinterpret_cast<QWidget*>(editor)), codeEditor(editor) {}

    QSize sizeHint() const override {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor *codeEditor;
};

} // namespace acutesim::gui::editors

