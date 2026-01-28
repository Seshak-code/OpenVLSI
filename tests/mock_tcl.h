#pragma once
#include <string>
#include <iostream>
#include <map>

// Mock Tcl API for testing without linking libtcl

typedef struct Tcl_Interp {
    std::string result;
} Tcl_Interp;

typedef int (*Tcl_CmdProc) (void *clientData, Tcl_Interp *interp, int argc, const char *argv[]);
typedef void *ClientData;

#define TCL_OK 0
#define TCL_ERROR 1
#define TCL_STATIC ((Tcl_FreeProc *) 0)
#define TCL_VOLATILE ((Tcl_FreeProc *) 1)
typedef void Tcl_FreeProc(char *blockPtr);

// Mock storage for commands
inline std::map<std::string, Tcl_CmdProc>& getMockCommands() {
    static std::map<std::string, Tcl_CmdProc> cmds;
    return cmds;
}
inline std::map<std::string, void*>& getMockClientData() {
    static std::map<std::string, void*> data;
    return data;
}

inline void Tcl_CreateCommand(Tcl_Interp *interp, const char *cmdName, Tcl_CmdProc proc, ClientData clientData, Tcl_FreeProc *deleteProc) {
    getMockCommands()[cmdName] = proc;
    getMockClientData()[cmdName] = clientData;
}

inline void Tcl_SetResult(Tcl_Interp *interp, char *result, Tcl_FreeProc *freeProc) {
    if (interp) interp->result = result;
}

inline const char *Tcl_GetStringResult(Tcl_Interp *interp) {
    return interp ? interp->result.c_str() : "";
}

inline Tcl_Interp* Tcl_CreateInterp() {
    return new Tcl_Interp();
}

inline int Tcl_Eval(Tcl_Interp *interp, const char *script) {
    std::cout << "[Mock TCL] Eval: " << script << std::endl;
    return TCL_OK;
}
