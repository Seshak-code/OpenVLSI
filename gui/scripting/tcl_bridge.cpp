#include "tcl_bridge.h"
#include <iostream>
#include <tcl.h> // Assumes system Tcl is available. If not, we might need a stub.

namespace acutesim {
namespace gui {
namespace scripting {

TclBridge& TclBridge::instance() {
    static TclBridge inst;
    return inst;
}

void TclBridge::init(Tcl_Interp* interp) {
    Tcl_CreateCommand(interp, "db_create_net", cmd_create_net, (ClientData)this, NULL);
    Tcl_CreateCommand(interp, "db_create_inst", cmd_create_inst, (ClientData)this, NULL);
    Tcl_CreateCommand(interp, "db_connect_pin", cmd_connect_pin, (ClientData)this, NULL);
    Tcl_CreateCommand(interp, "db_set_param", cmd_set_param, (ClientData)this, NULL);
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


}
}
}
