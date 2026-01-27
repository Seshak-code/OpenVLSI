#pragma once

#include <string>
#include <vector>
#include <functional>
#include "../../components/design_db.h"

// Forward declaration to avoid Tcl header dependency in global scope
typedef struct Tcl_Interp Tcl_Interp;

namespace acutesim {
namespace gui {
namespace scripting {

class TclBridge {
public:
    static TclBridge& instance();

    // Initialize Tcl interpreter and register commands
    void init(Tcl_Interp* interp = nullptr);
    
    // Execute a Tcl command string and return result/output
    std::string execute(const std::string& cmd);

    // Command Handlers
    static int cmd_create_net(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_rename_net(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_list_nets(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_create_inst(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_connect_pin(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_set_param(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_undo(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    static int cmd_redo(void* clientData, Tcl_Interp* interp, int argc, const char** argv);
    
    // DesignDb Access
    void setDesignDb(acutesim::components::DesignDb* db) { db_ = db; }
    
    // Accessor for the internal interpreter (if managed internally)
    // For now we assume init() is called with an external one, OR we create one lazily.
    // Let's store the interp.
    Tcl_Interp* getInterp() const { return interp_; }

private:
    TclBridge() = default;
    acutesim::components::DesignDb* db_ = nullptr;
    Tcl_Interp* interp_ = nullptr;
};

} // namespace scripting
} // namespace gui
} // namespace acutesim
