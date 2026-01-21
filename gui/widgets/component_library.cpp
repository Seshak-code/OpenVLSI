#include "component_library.h"

ComponentLibrary::ComponentLibrary(QWidget* parent) : QWidget(parent) {
    setupUi();
    populateLists();
    
    // Default to Bench mode
    setMode(AppMode::BENCH_TEST);
}

void ComponentLibrary::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    basicList = new QListWidget();
    activeList = new QListWidget();
    sourcesList = new QListWidget();
    interfacesList = new QListWidget();
    systemList = new QListWidget();
    icsList = new QListWidget();

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
    int srcIdx = tabs->indexOf(sourcesList);
    int intIdx = tabs->indexOf(interfacesList);
    int pdnIdx = tabs->indexOf(systemList);
    int icsIdx = tabs->indexOf(icsList);
    
    switch (mode) {
        case AppMode::BENCH_TEST:
            // Ideal sources, implicit ground, device testing
            tabs->setTabVisible(srcIdx, true);
            tabs->setTabVisible(intIdx, false);
            tabs->setTabVisible(pdnIdx, false);
            tabs->setTabVisible(icsIdx, false);
            break;
            
        case AppMode::BLOCK_SUBSYSTEM:
            // PowerPort interfaces, internal dependent sources
            tabs->setTabVisible(srcIdx, false);
            tabs->setTabVisible(intIdx, true);
            tabs->setTabVisible(pdnIdx, false);
            tabs->setTabVisible(icsIdx, false);
            break;
            
        case AppMode::SYSTEM_INTEGRATION:
            // RailDrivers only, explicit VSS/VDD symbols
            tabs->setTabVisible(srcIdx, false);
            tabs->setTabVisible(intIdx, false);
            tabs->setTabVisible(pdnIdx, true);
            tabs->setTabVisible(icsIdx, true);
            break;
    }
}
