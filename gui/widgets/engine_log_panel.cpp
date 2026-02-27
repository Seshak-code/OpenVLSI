#include "engine_log_panel.h"
#include <QHeaderView>
#include <QFileDialog>
#include <QTextStream>
#include <QDateTime>
#include <QHBoxLayout>

namespace acutesim {
namespace gui {
namespace widgets {

static QString typeToString(acutesim::compute::orchestration::EngineEventDTO::Type t) {
    using T = acutesim::compute::orchestration::EngineEventDTO::Type;
    switch (t) {
        case T::Info:        return "INFO";
        case T::Warning:     return "WARN";
        case T::Error:       return "ERROR";
        case T::Convergence: return "CONV";
    }
    return "UNKNOWN";
}

static QColor typeToColor(acutesim::compute::orchestration::EngineEventDTO::Type t) {
    using T = acutesim::compute::orchestration::EngineEventDTO::Type;
    switch (t) {
        case T::Info:        return QColor(180, 210, 240);
        case T::Warning:     return QColor(255, 210, 100);
        case T::Error:       return QColor(255, 120, 120);
        case T::Convergence: return QColor(160, 240, 180);
    }
    return QColor(200, 200, 200);
}

EngineLogPanel::EngineLogPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void EngineLogPanel::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // Toolbar row
    auto* toolbar = new QHBoxLayout();

    filterCombo_ = new QComboBox();
    filterCombo_->addItem("All", -1);
    filterCombo_->addItem("Info",        static_cast<int>(compute::orchestration::EngineEventDTO::Type::Info));
    filterCombo_->addItem("Warning",     static_cast<int>(compute::orchestration::EngineEventDTO::Type::Warning));
    filterCombo_->addItem("Error",       static_cast<int>(compute::orchestration::EngineEventDTO::Type::Error));
    filterCombo_->addItem("Convergence", static_cast<int>(compute::orchestration::EngineEventDTO::Type::Convergence));
    connect(filterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EngineLogPanel::onFilterChanged);

    clearBtn_ = new QPushButton("Clear");
    connect(clearBtn_, &QPushButton::clicked, this, &EngineLogPanel::clear);

    exportBtn_ = new QPushButton("Export .log");
    connect(exportBtn_, &QPushButton::clicked, this, &EngineLogPanel::exportLog);

    toolbar->addWidget(filterCombo_);
    toolbar->addStretch();
    toolbar->addWidget(clearBtn_);
    toolbar->addWidget(exportBtn_);
    layout->addLayout(toolbar);

    // Table
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({"Time", "Type", "Source", "Message"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->setAlternatingRowColors(true);

    // Styling
    table_->setStyleSheet(R"(
        QTableWidget {
            background-color: #1e1e2e;
            color: #cdd6f4;
            border: none;
            font-family: 'JetBrains Mono', 'Fira Code', monospace;
            font-size: 11px;
        }
        QTableWidget::item:selected {
            background-color: #45475a;
        }
        QHeaderView::section {
            background-color: #313244;
            color: #bac2de;
            border: 1px solid #45475a;
            padding: 4px;
        }
    )");

    layout->addWidget(table_);
}

void EngineLogPanel::appendEvent(const compute::orchestration::EngineEventDTO& event) {
    allEvents_.append(event);

    // If filter is active and doesn't match, skip display
    if (currentFilter_ != -1 && static_cast<int>(event.type) != currentFilter_) {
        return;
    }

    int row = table_->rowCount();
    table_->insertRow(row);

    // Timestamp (convert epoch ms to human-readable)
    auto dtStr = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.timestamp))
                     .toString("hh:mm:ss.zzz");
    auto* timeItem = new QTableWidgetItem(dtStr);
    timeItem->setForeground(QColor(166, 173, 200));
    table_->setItem(row, 0, timeItem);

    // Type
    auto* typeItem = new QTableWidgetItem(typeToString(event.type));
    typeItem->setForeground(typeToColor(event.type));
    table_->setItem(row, 1, typeItem);

    // Source
    auto* srcItem = new QTableWidgetItem(QString::fromStdString(event.source));
    srcItem->setForeground(QColor(137, 180, 250));
    table_->setItem(row, 2, srcItem);

    // Message
    auto* msgItem = new QTableWidgetItem(QString::fromStdString(event.message));
    table_->setItem(row, 3, msgItem);

    // Auto-scroll to bottom
    table_->scrollToBottom();
}

void EngineLogPanel::clear() {
    allEvents_.clear();
    table_->setRowCount(0);
}

void EngineLogPanel::exportLog() {
    QString path = QFileDialog::getSaveFileName(this, "Export Engine Log", "engine.log", "Log Files (*.log);;All Files (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&file);
    for (const auto& event : allEvents_) {
        auto dt = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.timestamp))
                      .toString("yyyy-MM-dd hh:mm:ss.zzz");
        out << dt << " [" << typeToString(event.type) << "] "
            << QString::fromStdString(event.source) << ": "
            << QString::fromStdString(event.message) << "\n";
    }
}

void EngineLogPanel::onFilterChanged(int index) {
    currentFilter_ = filterCombo_->itemData(index).toInt();
    applyFilter();
}

void EngineLogPanel::applyFilter() {
    table_->setRowCount(0);
    for (const auto& event : allEvents_) {
        if (currentFilter_ == -1 || static_cast<int>(event.type) == currentFilter_) {
            // Re-add visible events
            int row = table_->rowCount();
            table_->insertRow(row);

            auto dtStr = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(event.timestamp))
                             .toString("hh:mm:ss.zzz");
            table_->setItem(row, 0, new QTableWidgetItem(dtStr));

            auto* typeItem = new QTableWidgetItem(typeToString(event.type));
            typeItem->setForeground(typeToColor(event.type));
            table_->setItem(row, 1, typeItem);

            table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(event.source)));
            table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(event.message)));
        }
    }
}

} // namespace widgets
} // namespace gui
} // namespace acutesim
