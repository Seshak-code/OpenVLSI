#include "component_library.h"

ComponentLibrary::ComponentLibrary(QWidget* parent) : QWidget(parent) {
    setupUi();
    populateLists();
}

void ComponentLibrary::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs = new QTabWidget(this);
    layout->addWidget(tabs);

    basicList = new QListWidget();
    activeList = new QListWidget();
    sourcesList = new QListWidget();

    tabs->addTab(basicList, "Basic");
    tabs->addTab(activeList, "Active");
    tabs->addTab(sourcesList, "Sources");
    
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

    // Sources
    addComponentItem(sourcesList, "Voltage Source", "vsource.png", ComponentType::VoltageSource);
}

void ComponentLibrary::addComponentItem(QListWidget* list, const QString& name, const QString& iconName, ComponentType type) {
    QListWidgetItem* item = new QListWidgetItem(name, list);
    // TODO: Add icons
    // item->setIcon(QIcon(":/icons/" + iconName));
    item->setData(Qt::UserRole, (int)type);
    
    // Connect click only once per list? No, better to connect signals from list
}
