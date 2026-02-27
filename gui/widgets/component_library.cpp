#include "component_library.h"
#include "../../components/standard_cells.h"
#include "../compute/model_registry.h"

ComponentLibrary::ComponentLibrary(QWidget* parent) : QWidget(parent) {
    setupUi();
    populateLists();

    // Show all component tabs - unified library (no mode filtering)
    // All tabs are visible by default after setupUi()
}

void ComponentLibrary::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search box at top
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Filter components...");
    searchEdit_->setClearButtonEnabled(true);
    searchEdit_->setStyleSheet(R"(
        QLineEdit {
            background: #3c3c3c;
            color: #ccc;
            border: 1px solid #454545;
            border-radius: 4px;
            padding: 6px 8px;
            margin: 4px;
            font-size: 12px;
        }
        QLineEdit:focus {
            border-color: #007ACC;
        }
    )");
    connect(searchEdit_, &QLineEdit::textChanged, this, &ComponentLibrary::onSearchTextChanged);
    layout->addWidget(searchEdit_);

    tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    // High-density styling for tabs
    tabs->setStyleSheet(R"(
        QTabWidget::pane { border: none; }
        QTabBar::tab {
            padding: 4px 8px;
            font-size: 11px;
            min-width: 40px;
        }
        QTabBar::tab:selected { background: #37373d; }
        QTabBar::tab:hover { background: #2a2d2e; }
    )");

    // High-density list widget style
    QString listStyle = R"(
        QListWidget {
            background: transparent;
            border: none;
            font-size: 11px;
            outline: none;
        }
        QListWidget::item {
            padding: 3px 6px;
            border: none;
        }
        QListWidget::item:hover { background: #2a2d2e; }
        QListWidget::item:selected { background: #094771; }
    )";

    basicList = new QListWidget();
    pdkList = new QListWidget();
    userList = new QListWidget();

    // Apply compact styling to all lists
    basicList->setStyleSheet(listStyle);
    pdkList->setStyleSheet(listStyle);
    userList->setStyleSheet(listStyle);

    // Set uniform item heights for density
    basicList->setUniformItemSizes(true);
    pdkList->setUniformItemSizes(true);
    userList->setUniformItemSizes(true);

    tabs->addTab(basicList, "Primitives");
    tabs->addTab(pdkList, "Standard Cells");
    tabs->addTab(userList, "Verilog-A");
    
    // Connect Signals
    auto setupConnection = [this](QListWidget* list) {
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
            ComponentType type = (ComponentType)item->data(Qt::UserRole).toInt();
            QString subName = item->data(Qt::UserRole + 1).toString();
            emit componentSelected(type, subName);
        });
    };
    
    setupConnection(basicList);
    setupConnection(pdkList);
    setupConnection(userList);
}

void ComponentLibrary::refresh() {
    userList->clear();
    
    // Scan ModelRegistry for non-standard models
    std::vector<std::string> deviceTypes = ModelRegistry::instance().listDeviceTypes();
    for (const auto& type : deviceTypes) {
        // Skip standard types usually (handled manually in basicList)
        // But if user overwrites them?
        // Let's filter out known primitives to avoid duplication if registry has them?
        // Actually, basic properties are hardcoded in populateLists.
        // We only want Dynamic External Models here.
        // Heuristic: If it's not in a "Standard Library" exclude list?
        // For now, list everything found in Registry that matches "Generic" usage pattern.
        // Or simply list everything from Registry.
        
        std::vector<std::string> models = ModelRegistry::instance().listModels(type);
        for (const auto& name : models) {
            // Check if this is a Verilog-A model
            // We can assume any model in Registry is a candidate for "User Models" if not explicitly excluded.
            // Let's just list them.
            addComponentItem(userList, QString::fromStdString(name), "chip.png", ComponentType::Generic);
        }
    }
}

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QCoreApplication>
#include <QDir>

static ComponentType componentTypeFromString(const QString& s) {
    static const QMap<QString, ComponentType> map = {
        {"Resistor", ComponentType::Resistor},
        {"Capacitor", ComponentType::Capacitor},
        {"Inductor", ComponentType::Inductor},
        {"Ground", ComponentType::Ground},
        {"VoltageSource", ComponentType::VoltageSource},
        {"CurrentSource", ComponentType::CurrentSource},
        {"PDK_VDD", ComponentType::PDK_VDD},
        {"PDK_VSS", ComponentType::PDK_VSS},
        {"PDK_VDDIO", ComponentType::PDK_VDDIO},
        {"PDK_VBB", ComponentType::PDK_VBB},
        {"PDK_NFET", ComponentType::PDK_NFET},
        {"PDK_PFET", ComponentType::PDK_PFET},
        {"SubCircuit", ComponentType::SubCircuit},
        {"Generic", ComponentType::Generic},
    };
    return map.value(s, ComponentType::Generic);
}

bool ComponentLibrary::loadFromJsonRegistry() {
    // Search for the JSON registry in known locations
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/resources/component_library.json",
        QCoreApplication::applicationDirPath() + "/../resources/component_library.json",
        "resources/component_library.json",
    };

    QFile file;
    for (const auto& path : searchPaths) {
        file.setFileName(path);
        if (file.exists()) break;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[ComponentLibrary] JSON registry not found, using hardcoded fallback";
        return false;
    }

    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[ComponentLibrary] JSON parse error:" << err.errorString();
        return false;
    }

    auto root = doc.object();
    auto categories = root["categories"].toArray();

    for (const auto& catVal : categories) {
        auto cat = catVal.toObject();
        QString tab = cat["tab"].toString();
        auto components = cat["components"].toArray();

        QListWidget* targetList = nullptr;
        if (tab == "Primitives") targetList = basicList;
        else if (tab == "Standard Cells") targetList = pdkList;
        else if (tab == "Verilog-A") targetList = userList;
        else targetList = basicList;

        for (const auto& compVal : components) {
            auto comp = compVal.toObject();
            addComponentItem(
                targetList,
                comp["displayName"].toString(),
                comp["icon"].toString(),
                componentTypeFromString(comp["type"].toString())
            );
        }
    }

    qDebug() << "[ComponentLibrary] Loaded from JSON registry";
    return true;
}

void ComponentLibrary::populateLists() {
    // Try JSON registry first
    if (loadFromJsonRegistry()) {
        // Also load dynamic PDK cells from StandardCellLibrary
        auto cellNames = StandardCells::StandardCellLibrary::instance().getNames();
        for (const auto& name : cellNames) {
            // Skip cells already loaded from JSON
            bool alreadyExists = false;
            for (int i = 0; i < pdkList->count(); ++i) {
                if (pdkList->item(i)->text() == QString::fromStdString(name)) {
                    alreadyExists = true;
                    break;
                }
            }
            if (!alreadyExists) {
                ComponentType type = ComponentType::SubCircuit;
                QString icon = "chip.png";
                if (name == "PDK_NFET") { type = ComponentType::PDK_NFET; icon = "nfet.png"; }
                else if (name == "PDK_PFET") { type = ComponentType::PDK_PFET; icon = "pfet.png"; }
                addComponentItem(pdkList, QString::fromStdString(name), icon, type);
            }
        }
        refresh();
        return;
    }

    // ── Fallback: hardcoded population ──────────────────────────────────────
    addComponentItem(basicList, "Resistor", "resistor.png", ComponentType::Resistor);
    addComponentItem(basicList, "Capacitor", "capacitor.png", ComponentType::Capacitor);
    addComponentItem(basicList, "Inductor", "inductor.png", ComponentType::Inductor);
    addComponentItem(basicList, "Ground", "ground.png", ComponentType::Ground);
    addComponentItem(basicList, "VDD Rail", "vdd.png", ComponentType::PDK_VDD);
    addComponentItem(basicList, "VSS (Ground)", "vss.png", ComponentType::PDK_VSS);
    addComponentItem(basicList, "VDDIO Rail", "vddio.png", ComponentType::PDK_VDDIO);
    addComponentItem(basicList, "VBB (Body Bias)", "vbb.png", ComponentType::PDK_VBB);
    addComponentItem(basicList, "Voltage Source", "vsource.png", ComponentType::VoltageSource);
    addComponentItem(basicList, "Current Source", "isource.png", ComponentType::CurrentSource);

    auto cellNames = StandardCells::StandardCellLibrary::instance().getNames();
    for (const auto& name : cellNames) {
        ComponentType type = ComponentType::SubCircuit;
        QString icon = "chip.png";
        if (name == "PDK_NFET") { type = ComponentType::PDK_NFET; icon = "nfet.png"; }
        else if (name == "PDK_PFET") { type = ComponentType::PDK_PFET; icon = "pfet.png"; }
        else if (name == "PDK_Resistor_Poly") { type = ComponentType::PDK_Resistor; icon = "res.png"; }
        else if (name == "PDK_Capacitor_MIM") { type = ComponentType::PDK_Capacitor; icon = "cap.png"; }
        else if (name.find("Inverter") != std::string::npos) { icon = "inverter.png"; }
        addComponentItem(pdkList, QString::fromStdString(name), icon, type);
    }

    refresh();
}

void ComponentLibrary::addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type) {
    QListWidgetItem* item = new QListWidgetItem(name, list);
    // TODO: Add actual icons
    // item->setIcon(QIcon(":/icons/" + iconName));
    item->setData(Qt::UserRole, (int)type);
    
    // If it is a SubCircuit or PDK component, store the name as subCircuit identifier
    // For simplicity, we store 'name' as identifier for SubCircuits and PDK types sourced from lib
    if (type == ComponentType::SubCircuit || 
        type == ComponentType::PDK_NFET || 
        type == ComponentType::PDK_PFET ||
        type == ComponentType::PDK_Resistor ||
        type == ComponentType::PDK_Capacitor ||
        type == ComponentType::Generic) {
        item->setData(Qt::UserRole + 1, name);
    }
}

// setStage() removed - unified library
// setMode() removed - unified component library

// setMode() removed - unified component library (all tabs always visible)

void ComponentLibrary::onSearchTextChanged(const QString& text) {
    QString pattern = text.trimmed().toLower();
    filterList(basicList, pattern);
    filterList(pdkList, pattern);
    filterList(userList, pattern);
}

void ComponentLibrary::filterList(QListWidget* list, const QString& pattern) {
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        bool matches = pattern.isEmpty() ||
                       item->text().toLower().contains(pattern);
        item->setHidden(!matches);
    }
}
