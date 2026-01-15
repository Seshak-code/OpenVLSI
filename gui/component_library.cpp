#include "component_library.h"

ComponentLibrary::ComponentLibrary(QWidget* parent) : QWidget(parent) {
    setupUi();
    populateLists();
    populateSystemList();
    
    // Default to RTL stage (hides System tab)
    setStage(DesignStage::RTL_DESIGN);
}

void ComponentLibrary::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    basicList = new QListWidget();
    activeList = new QListWidget();
    sourcesList = new QListWidget();
    systemList = new QListWidget();

    tabs->addTab(basicList, "Basic");
    tabs->addTab(activeList, "Active");
    tabs->addTab(sourcesList, "Sources");
    tabs->addTab(systemList, "PDN"); // Power Delivery Network parts
    
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
    setupConnection(systemList);
}

void ComponentLibrary::populateLists() {
    // Basic Components
    addComponentItem(basicList, "Resistor", "resistor.png", ComponentType::Resistor);
    addComponentItem(basicList, "Capacitor", "capacitor.png", ComponentType::Capacitor);
    addComponentItem(basicList, "Inductor", "inductor.png", ComponentType::Inductor);
    addComponentItem(basicList, "Ground", "ground.png", ComponentType::Ground);

    // Active Components
    addComponentItem(activeList, "Diode", "diode.png", ComponentType::Diode);
    addComponentItem(activeList, "N-MOSFET", "nmos.png", ComponentType::Mosfet);
    addComponentItem(activeList, "NPN BJT", "bjt.png", ComponentType::Bjt);
    addComponentItem(activeList, "Schottky Diode", "schottky.png", ComponentType::SchottkyDiode);

    // Sources (Ideal lab sources)
    addComponentItem(sourcesList, "Voltage Source", "vsource.png", ComponentType::VoltageSource);
    addComponentItem(sourcesList, "Current Source", "isource.png", ComponentType::CurrentSource);
}

void ComponentLibrary::populateSystemList() {
    // PDN-specific parts (visible in Physical Design stage)
    addComponentItem(systemList, "Power Rail", "powerrail.png", ComponentType::VoltageSource); // Reuses VoltageSource for now
    addComponentItem(systemList, "Decoupling Cap", "decap.png", ComponentType::Capacitor);
    addComponentItem(systemList, "Ferrite Bead", "ferrite.png", ComponentType::Inductor); // Models as inductor
}

void ComponentLibrary::addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type) {
    QListWidgetItem* item = new QListWidgetItem(name, list);
    // TODO: Add icons
    // item->setIcon(QIcon(":/icons/" + iconName));
    item->setData(Qt::UserRole, (int)type);
}

void ComponentLibrary::setStage(DesignStage stage) {
    // RTL/Synthesis: Show ideal sources, hide PDN
    // Physical/Verification: Show PDN parts
    int pdnIdx = tabs->indexOf(systemList);
    int srcIdx = tabs->indexOf(sourcesList);
    
    switch (stage) {
        case DesignStage::RTL_DESIGN:
        case DesignStage::SYNTHESIS:
            tabs->setTabVisible(pdnIdx, false);
            tabs->setTabVisible(srcIdx, true);
            break;
        case DesignStage::PHYSICAL_DESIGN:
        case DesignStage::VERIFICATION:
            tabs->setTabVisible(pdnIdx, true);
            tabs->setTabVisible(srcIdx, false); // Hide ideal sources in physical stage
            break;
    }
}
