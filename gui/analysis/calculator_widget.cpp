#include "calculator_widget.h"
#include <string>
#include <unordered_map>
#include <QVBoxLayout>
#include <QDebug>

namespace acutesim::gui::analysis {

CalculatorWidget::CalculatorWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
}

void CalculatorWidget::setWaveforms(const std::unordered_map<std::string, acutesim::compute::postproc::Waveform>& waves) {
    engine_.setWaveforms(waves);
    waveList_->clear();
    for (const auto& [key, _] : waves) {
        waveList_->addItem(QString::fromStdString(key));
    }
}

void CalculatorWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    
    // 1. Expression Input
    exprInput_ = new QLineEdit(this);
    exprInput_->setPlaceholderText("Enter expression... e.g. dB20(v(\"/out\"))");
    layout->addWidget(exprInput_);
    
    // 2. Waveform Selector
    waveList_ = new QListWidget(this);
    connect(waveList_, &QListWidget::itemDoubleClicked, this, &CalculatorWidget::onWaveSelected);
    layout->addWidget(waveList_, 1);
    
    // 3. Keypad
    QGridLayout* grid = new QGridLayout();
    // Numeric
    grid->addWidget(createBtn("7"), 0, 0); grid->addWidget(createBtn("8"), 0, 1); grid->addWidget(createBtn("9"), 0, 2);
    grid->addWidget(createBtn("4"), 1, 0); grid->addWidget(createBtn("5"), 1, 1); grid->addWidget(createBtn("6"), 1, 2);
    grid->addWidget(createBtn("1"), 2, 0); grid->addWidget(createBtn("2"), 2, 1); grid->addWidget(createBtn("3"), 2, 2);
    grid->addWidget(createBtn("0"), 3, 1); grid->addWidget(createBtn("."), 3, 2);
    
    // Ops
    grid->addWidget(createBtn("+"), 0, 3); grid->addWidget(createBtn("-"), 1, 3);
    grid->addWidget(createBtn("*"), 2, 3); grid->addWidget(createBtn("/"), 3, 3);
    
    // Funcs
    grid->addWidget(createBtn("dB20", "dB20("), 0, 4);
    grid->addWidget(createBtn("mag", "mag("), 1, 4);
    grid->addWidget(createBtn("phase", "phase("), 2, 4);
    grid->addWidget(createBtn("avg", "average("), 3, 4);
    grid->addWidget(createBtn("(", "("), 4, 0);
    grid->addWidget(createBtn(")", ")"), 4, 1);
    
    // Special
    QPushButton* evalBtn = new QPushButton("Eval", this);
    connect(evalBtn, &QPushButton::clicked, this, &CalculatorWidget::onEvalClicked);
    grid->addWidget(evalBtn, 4, 3, 1, 2);
    
    layout->addLayout(grid);
}

QPushButton* CalculatorWidget::createBtn(const QString& text, const QString& insertText) {
    QPushButton* btn = new QPushButton(text, this);
    QString toInsert = insertText.isEmpty() ? text : insertText;
    btn->setProperty("insert", toInsert);
    connect(btn, &QPushButton::clicked, this, &CalculatorWidget::onBtnClicked);
    return btn;
}

void CalculatorWidget::onBtnClicked() {
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (btn) {
        QString text = btn->property("insert").toString();
        exprInput_->insert(text);
        exprInput_->setFocus();
    }
}

void CalculatorWidget::onWaveSelected() {
    auto items = waveList_->selectedItems();
    if (!items.isEmpty()) {
        // List entries are waveform keys, which are already v(...) expressions
        exprInput_->insert(items[0]->text());
        exprInput_->setFocus();
    }
}

void CalculatorWidget::onEvalClicked() {
    QString expr = exprInput_->text();
    // Use engine to parse/eval
    auto res = engine_.evaluate(expr.toStdString());
    emit expressionEvaluated(expr, res);
}

} // namespace acutesim::gui::analysis
