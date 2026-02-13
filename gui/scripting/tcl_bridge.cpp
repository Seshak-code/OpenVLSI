#include "tcl_bridge.h"
#include <iostream>
#ifdef TEST_MODE
#include "../../tests/mock_tcl.h"
#else
#include <tcl.h> // Assumes system Tcl is available.
#endif
#include "../editors/schematic/schematic_editor.h" // Include full definition for casting
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace acutesim {
namespace gui {
namespace scripting {

using namespace acutesim::gui::scripting;
using namespace acutesim::components;

// Helper to convert string to Direction
static SchematicEditor::Direction stringToDir(const std::string& s) {
    if (s == "North") return SchematicEditor::Direction::North;
    if (s == "South") return SchematicEditor::Direction::South;
    if (s == "East")  return SchematicEditor::Direction::East;
    if (s == "West")  return SchematicEditor::Direction::West;
    return SchematicEditor::Direction::East; // Default
}

TclBridge& TclBridge::instance() {
    static TclBridge inst;
    return inst;
}

void TclBridge::init(Tcl_Interp* interp) {
    if (interp) {
        interp_ = interp;
    } else if (!interp_) {
        interp_ = Tcl_CreateInterp();
    }
    
    if (!interp_) return;

    Tcl_CreateCommand(interp_, "db_create_net", cmd_create_net, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "db_rename_net", cmd_rename_net, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "db_list_nets", cmd_list_nets, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "db_connect_pin", cmd_connect_pin, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "db_set_param", cmd_set_param, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "db_undo", cmd_undo, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "db_redo", cmd_redo, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "place_next_to", cmd_place_next_to, (ClientData)this, NULL);
    
    // Phase 2.1: Simulation Commands
    Tcl_CreateCommand(interp_, "get_voltage", cmd_get_voltage, (ClientData)this, NULL);
    Tcl_CreateCommand(interp_, "list_probes", cmd_list_probes, (ClientData)this, NULL);
}

std::string TclBridge::execute(const std::string& cmd) {
    if (!interp_) return "Error: Tcl Interpreter not initialized";
    
    int code = Tcl_Eval(interp_, cmd.c_str());
    const char* result = Tcl_GetStringResult(interp_);
    
    if (code != TCL_OK) {
        return "Error: " + std::string(result);
    }
    return result;
}

// db_create_net <name>
int TclBridge::cmd_create_net(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->db_) {
        Tcl_SetResult(interp, (char*)"Error: DesignDb not connected", TCL_STATIC);
        return TCL_ERROR;
    }
    if (argc != 2) {
        Tcl_SetResult(interp, (char*)"Usage: db_create_net <name>", TCL_STATIC);
        return TCL_ERROR;
    }
    
    std::string name = argv[1];
    auto id = self->db_->createNet(name);
    
    std::string result = id.toString();
    Tcl_SetResult(interp, (char*)result.c_str(), TCL_VOLATILE);
    return TCL_OK;
}

// db_rename_net <old_name> <new_name>
int TclBridge::cmd_rename_net(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->db_) return TCL_ERROR;
    if (argc != 3) {
        Tcl_SetResult(interp, (char*)"Usage: db_rename_net <old_name> <new_name>", TCL_STATIC);
        return TCL_ERROR;
    }
    
    std::string oldName = argv[1];
    std::string newName = argv[2];
    
    auto result = self->db_->renameNet(oldName, newName);
    
    using namespace acutesim::components;
    
    if (result == MutationResult::Success) {
        Tcl_SetResult(interp, (char*)"1", TCL_STATIC);
        return TCL_OK;
    } else if (result == MutationResult::NameCollision) {
        Tcl_SetResult(interp, (char*)"Error: Name collision", TCL_STATIC);
        return TCL_ERROR;
    } else {
        Tcl_SetResult(interp, (char*)"Error: Net not found", TCL_STATIC);
        return TCL_ERROR;
    }
}

int TclBridge::cmd_list_nets(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->db_) return TCL_ERROR;

    std::string result;
    const auto& nets = self->db_->getAllNets();
    for (const auto& pair : nets) {
        if (!result.empty()) result += " ";
        result += pair.second.name;
    }
    
    Tcl_SetResult(interp, (char*)result.c_str(), TCL_VOLATILE);
    return TCL_OK;
}

// db_undo
int TclBridge::cmd_undo(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    // Stub implementation
    Tcl_SetResult(interp, (char*)"Undo not yet implemented.", TCL_STATIC);
    return TCL_OK;
}

// db_redo
int TclBridge::cmd_redo(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    // Stub
    Tcl_SetResult(interp, (char*)"Redo not implemented", TCL_STATIC);
    return TCL_OK;
}

int TclBridge::cmd_place_next_to(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->editor_) {
        Tcl_SetResult(interp, (char*)"Error: Schematic Editor not attached", TCL_STATIC);
        return TCL_ERROR;
    }
    
    if (argc != 4) {
        Tcl_SetResult(interp, (char*)"Usage: place_next_to <refId> <newType> <direction>", TCL_STATIC);
        return TCL_ERROR;
    }
    
    const char* refId = argv[1];
    const char* newType = argv[2];
    const char* dirStr = argv[3];
    
    SchematicEditor* editor = static_cast<SchematicEditor*>(self->editor_);
    
    // Qt interaction usually needs to be on main thread.
    // If Tcl is running on a worker thread, this is unsafe.
    // Assuming for now Tcl is running on main thread via invokeMethod or similar in the test harness/console.
    // In strict threading model, we would use QMetaObject::invokeMethod.
    
    editor->placeNextTo(QString(refId), QString(newType), stringToDir(dirStr));
    
    Tcl_SetResult(interp, (char*)"Placed", TCL_STATIC);
    return TCL_OK;
}

// db_create_inst <master> <name>
int TclBridge::cmd_create_inst(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->db_) return TCL_ERROR;
    if (argc != 3) {
        Tcl_SetResult(interp, (char*)"Usage: db_create_inst <master> <name>", TCL_STATIC);
        return TCL_ERROR;
    }
    
    std::string master = argv[1];
    std::string name = argv[2];
    auto id = self->db_->createInstance(master, name);
    
    Tcl_SetResult(interp, (char*)id.toString().c_str(), TCL_VOLATILE);
    return TCL_OK;
}

// db_connect_pin <inst_uuid> <terminal_idx> <net_uuid>
int TclBridge::cmd_connect_pin(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->db_) return TCL_ERROR;
    if (argc != 4) return TCL_ERROR;
    
    using namespace acutesim::components;
    
    InstUUID instId = InstUUID::fromString(argv[1]);
    int term = atoi(argv[2]);
    NetUUID netId = NetUUID::fromString(argv[3]);
    
    self->db_->connectPin(instId, term, netId);
    
    return TCL_OK;
}

// db_set_param <inst_uuid> <key> <value>
int TclBridge::cmd_set_param(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (!self->db_) return TCL_ERROR;
    if (argc != 4) return TCL_ERROR;
    
    using namespace acutesim::components;

    InstUUID instId = InstUUID::fromString(argv[1]);
    std::string key = argv[2];
    double val = atof(argv[3]);
    
    self->db_->setParam(instId, key, val);
    
    return TCL_OK;
}

// get_voltage <name_or_index>
int TclBridge::cmd_get_voltage(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (argc != 2) {
        Tcl_SetResult(interp, (char*)"Usage: get_voltage <name_or_index>", TCL_STATIC);
        return TCL_ERROR;
    }
    
    if (self->lastResultsJson_.empty()) {
        Tcl_SetResult(interp, (char*)"Error: No simulation results available", TCL_STATIC);
        return TCL_ERROR;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(self->lastResultsJson_));
    QJsonObject obj = doc.object();
    
    std::string query = argv[1];
    
    // Check Probes first
    if (obj.contains("probes")) {
        QJsonObject probes = obj["probes"].toObject();
        if (probes.contains(QString::fromStdString(query))) {
            double val = probes[QString::fromStdString(query)].toDouble();
            Tcl_SetObjResult(interp, Tcl_NewDoubleObj(val));
            return TCL_OK;
        }
    }
    
    // Check Voltages (by index or V(n) format)
    if (obj.contains("voltages")) {
        QJsonArray volts = obj["voltages"].toArray();
        int idx = -1;
        
        if (isdigit(query[0])) {
            idx = atoi(query.c_str()) - 1; // 1-based to 0-based
        } else if (query.size() > 2 && query.substr(0, 2) == "V(" && query.back() == ')') {
            std::string num = query.substr(2, query.size() - 3);
            idx = atoi(num.c_str()) - 1;
        }
        
        if (idx >= 0 && idx < volts.size()) {
            double val = volts[idx].toDouble();
            Tcl_SetObjResult(interp, Tcl_NewDoubleObj(val));
            return TCL_OK;
        }
    }
    
    Tcl_SetResult(interp, (char*)"Error: Node/Probe not found", TCL_STATIC);
    return TCL_ERROR;
}

// list_probes
int TclBridge::cmd_list_probes(void* clientData, Tcl_Interp* interp, int argc, const char** argv) {
    TclBridge* self = (TclBridge*)clientData;
    if (self->lastResultsJson_.empty()) {
        Tcl_SetResult(interp, (char*)"", TCL_STATIC);
        return TCL_OK;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(self->lastResultsJson_));
    QJsonObject obj = doc.object();
    
    if (obj.contains("probes")) {
        QJsonObject probes = obj["probes"].toObject();
        std::string result;
        for (auto it = probes.begin(); it != probes.end(); ++it) {
            if (!result.empty()) result += " ";
            result += it.key().toStdString();
        }
        Tcl_SetResult(interp, (char*)result.c_str(), TCL_VOLATILE);
    } else {
        Tcl_SetResult(interp, (char*)"", TCL_STATIC);
    }
    
    return TCL_OK;
}


}
}
}
