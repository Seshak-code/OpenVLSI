#include "component_library.h"

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
    activeList = new QListWidget();
    sourcesList = new QListWidget();
    interfacesList = new QListWidget();
    systemList = new QListWidget();
    icsList = new QListWidget();

    // Apply compact styling to all lists
    basicList->setStyleSheet(listStyle);
    activeList->setStyleSheet(listStyle);
    sourcesList->setStyleSheet(listStyle);
    interfacesList->setStyleSheet(listStyle);
    systemList->setStyleSheet(listStyle);
    icsList->setStyleSheet(listStyle);

    // Set uniform item heights for density
    basicList->setUniformItemSizes(true);
    activeList->setUniformItemSizes(true);
    sourcesList->setUniformItemSizes(true);
    interfacesList->setUniformItemSizes(true);
    systemList->setUniformItemSizes(true);
    icsList->setUniformItemSizes(true);

    tabs->addTab(basicList, "Basic");
    tabs->addTab(activeList, "Active");
    tabs->addTab(sourcesList, "Sources");      // Bench: ideal V/I, dependent
    tabs->addTab(interfacesList, "Interfaces"); // Block: PowerPort pins
    tabs->addTab(systemList, "PDN");           // System: RailDrivers, Vdd/Vss
    tabs->addTab(icsList, "ICs");              // System: IC packages
    
    // Connect Signals
    auto setupConnection = [this](QListWidget* list) {
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
            ComponentType type = (ComponentType)item->data(Qt::UserRole).toInt();
            emit componentSelected(type);
        });
    };
    
    setupConnection(basicList);
    setupConnection(activeList);
    setupConnection(sourcesList);
    setupConnection(interfacesList);
    setupConnection(systemList);
    setupConnection(icsList);
}

void ComponentLibrary::populateLists() {
    // =========================================================================
    // BASIC - Available in all modes
    // =========================================================================
    addComponentItem(basicList, "Resistor", "resistor.png", ComponentType::Resistor);
    addComponentItem(basicList, "Capacitor", "capacitor.png", ComponentType::Capacitor);
    addComponentItem(basicList, "Inductor", "inductor.png", ComponentType::Inductor);
    // Ground omitted: implicit in Bench, pin in Block, Vss in System

    // =========================================================================
    // ACTIVE - Available in all modes
    // =========================================================================
    addComponentItem(activeList, "Diode", "diode.png", ComponentType::Diode);
    addComponentItem(activeList, "N-MOSFET", "nmos.png", ComponentType::Mosfet);
    addComponentItem(activeList, "NPN BJT", "bjt.png", ComponentType::Bjt);
    addComponentItem(activeList, "N-JFET", "njfet.png", ComponentType::JFET_N);
    addComponentItem(activeList, "P-JFET", "pjfet.png", ComponentType::JFET_P);
    addComponentItem(activeList, "Schottky Diode", "schottky.png", ComponentType::SchottkyDiode);

    // =========================================================================
    // SOURCES - Bench mode only (ideal + dependent)
    // =========================================================================
    addComponentItem(sourcesList, "Voltage Source", "vsource.png", ComponentType::VoltageSource);
    addComponentItem(sourcesList, "Current Source", "isource.png", ComponentType::CurrentSource);
    addComponentItem(sourcesList, "VCVS", "vcvs.png", ComponentType::VCVS);
    addComponentItem(sourcesList, "CCVS", "ccvs.png", ComponentType::CCVS);
    addComponentItem(sourcesList, "VCCS", "vccs.png", ComponentType::VCCS);
    addComponentItem(sourcesList, "CCCS", "cccs.png", ComponentType::CCCS);

    // =========================================================================
    // INTERFACES - Block mode only (PowerPort never stamps MNA)
    // =========================================================================
    addComponentItem(interfacesList, "Vin", "pin.png", ComponentType::PowerPort);
    addComponentItem(interfacesList, "Vout", "pin.png", ComponentType::PowerPort);
    addComponentItem(interfacesList, "GND Pin", "gnd_pin.png", ComponentType::PowerPort);
    addComponentItem(interfacesList, "VDD Pin", "vdd_pin.png", ComponentType::PowerPort);

    // =========================================================================
    // PDN - System mode only (RailDrivers inject net power)
    // =========================================================================
    addComponentItem(systemList, "Vdd/Vcc", "vdd.png", ComponentType::PowerVdd);
    addComponentItem(systemList, "Vss/GND", "vss.png", ComponentType::PowerVss);
    addComponentItem(systemList, "Battery", "battery.png", ComponentType::RailDriver);
    addComponentItem(systemList, "LDO Regulator", "ldo.png", ComponentType::RailDriver);
    addComponentItem(systemList, "Waveform Gen", "waveform.png", ComponentType::WaveformGen);
    addComponentItem(systemList, "Decoupling Cap", "decap.png", ComponentType::Capacitor);

    // =========================================================================
    // ICs - System mode only
    // =========================================================================
    addComponentItem(icsList, "ULN2003A", "uln2003a.png", ComponentType::ICPackage);
    addComponentItem(icsList, "ULN2803A", "uln2803a.png", ComponentType::ICPackage);
    addComponentItem(icsList, "LM7805", "lm7805.png", ComponentType::ICPackage);
    addComponentItem(icsList, "NE555", "ne555.png", ComponentType::ICPackage);
}

void ComponentLibrary::addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type) {
    QListWidgetItem* item = new QListWidgetItem(name, list);
    // TODO: Add icons
    // item->setIcon(QIcon(":/icons/" + iconName));
    item->setData(Qt::UserRole, (int)type);
}

void ComponentLibrary::setStage(DesignStage stage) {
    // Stage affects PDN visibility within System mode
    // Keeping for backwards compatibility
    (void)stage;
}

void ComponentLibrary::setMode(AppMode mode) {
    // DEPRECATED: Mode filtering removed - unified component library
    // All tabs are now always visible regardless of mode
    Q_UNUSED(mode);

    // Ensure all tabs are visible (unified library)
    int srcIdx = tabs->indexOf(sourcesList);
    int intIdx = tabs->indexOf(interfacesList);
    int pdnIdx = tabs->indexOf(systemList);
    int icsIdx = tabs->indexOf(icsList);

    tabs->setTabVisible(srcIdx, true);
    tabs->setTabVisible(intIdx, true);
    tabs->setTabVisible(pdnIdx, true);
    tabs->setTabVisible(icsIdx, true);
}

void ComponentLibrary::onSearchTextChanged(const QString& text) {
    QString pattern = text.trimmed().toLower();
    filterList(basicList, pattern);
    filterList(activeList, pattern);
    filterList(sourcesList, pattern);
    filterList(interfacesList, pattern);
    filterList(systemList, pattern);
    filterList(icsList, pattern);
}

void ComponentLibrary::filterList(QListWidget* list, const QString& pattern) {
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem* item = list->item(i);
        bool matches = pattern.isEmpty() ||
                       item->text().toLower().contains(pattern);
        item->setHidden(!matches);
    }
}
