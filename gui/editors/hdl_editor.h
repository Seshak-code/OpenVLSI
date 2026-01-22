#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

// ============================================================================
// HDL Syntax Highlighter
// Supports basic Verilog/VHDL/AMS keyword highlighting.
// ============================================================================

class HDLSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum Language { VERILOG, VHDL, VERILOG_AMS };
    
    explicit HDLSyntaxHighlighter(QTextDocument *parent = nullptr);
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

// ============================================================================
// HDL Editor Widget
// A code editor with line numbers and syntax highlighting.
// ============================================================================

class HDLEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit HDLEditor(QWidget *parent = nullptr);

    void setLanguage(HDLSyntaxHighlighter::Language lang);
    void lineNumberAreaPaintEvent(QPaintEvent *event);
    int lineNumberAreaWidth();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect &rect, int dy);

private:
    QWidget *lineNumberArea;
    HDLSyntaxHighlighter *highlighter;
    
    // Theme colors (VS Code Dark)
    const QColor COL_BG = QColor(30, 30, 30);
    const QColor COL_FG = QColor(220, 220, 220);
    const QColor COL_LINE_NUM = QColor(100, 100, 100);
    const QColor COL_CURRENT_LINE = QColor(40, 40, 40);
};

// Helper widget for line numbers
class LineNumberArea : public QWidget {
public:
    LineNumberArea(HDLEditor *editor) : QWidget(reinterpret_cast<QWidget*>(editor)), codeEditor(editor) {}

    QSize sizeHint() const override {
        return QSize(codeEditor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        codeEditor->lineNumberAreaPaintEvent(event);
    }

private:
    HDLEditor *codeEditor;
};
