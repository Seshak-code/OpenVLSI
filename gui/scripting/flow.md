# GUI Scripting Flow

The `gui/scripting/` directory contains the **Automation & Extension** layer of CuteApp. It bridges the C++ core with dynamic scripting languages (Tcl), enabling test automation, console interaction, and AI code generation.

## Key Files & Responsibilities

### 1. Tcl Bridge (`tcl_bridge.h`)
**Role**: The Interpreter. Embeds a Tcl interpreter to execute commands against the `DesignDb` and `SchematicEditor`.
-   **Command Registration**: Maps `db_` commands to C++ handlers.
-   **Output Handling**: Captures stdout/result for the Console.
-   **Test Harness**: Used by `test_auto_harness` to drive regressions.

```cpp
class TclBridge {
public:
    static TclBridge& instance();
    void init(Tcl_Interp* interp = nullptr);
    std::string execute(const std::string& cmd);

    // Command Handlers
    static int cmd_create_net(ClientData, Tcl_Interp*, int, const char**);
    static int cmd_list_nets(ClientData, Tcl_Interp*, int, const char**);
    static int cmd_rename_net(ClientData, Tcl_Interp*, int, const char**);
    static int cmd_undo(ClientData, Tcl_Interp*, int, const char**);
};
```

## Supported Commands

| Command | Arguments | Description |
| :--- | :--- | :--- |
| `db_create_net` | `name` | Creates a new net in the database. |
| `db_list_nets` | none | Returns a space-separated list of all net names. |
| `db_rename_net` | `oldName` `newName` | Renames an existing net. Updates both DB and GUI labels. |
| `db_connect_pin` | `inst` `pin` `net` | Connects a component pin to a net. |
| `db_undo` | none | Reverts the last transaction (Stub). |

## Data Flow
1.  **Input**: User types `db_list_nets` in the Terminal or AI generates a script.
2.  **Routing**: `MainWindow` passes string to `TclBridge::execute()`.
3.  **Execution**: `Tcl_Eval` calls the C++ callback `cmd_list_nets`.
4.  **Access**: Callback queries `DesignDb::getAllNets()`.
5.  **Output**: Result string is returned to `MainWindow` and printed to `OutputDock`.

## Development Rules
1.  **Stateless Handlers**: Static command handlers should look up the singleton/context; avoid stored state in the bridge.
2.  **String Safety**: Always use `std::string` for C++ <-> Tcl interoperability.
3.  **Error Propagation**: Return `TCL_ERROR` and set `Tcl_SetResult` for user-facing errors.
