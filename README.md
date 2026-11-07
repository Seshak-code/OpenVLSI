# OpenVLSI

**A macOS-first chip-design suite whose tools match Cadence's, block for block.** TreeSPICE is the simulator for
everything; Yosys, OpenROAD, KLayout, Netgen, OpenRCX and OpenSTA are the implementation and sign-off backends, used
as they are. OpenVLSI is the frontend that ties them together: one design database, one command layer, and
Cadence-style tools over them.

> **Status:** one window takes a design from Verilog to a signed-off GDS and tests the result, down to the routed chip at transistor level on TreeSPICE; analog from schematic through ADE
> to a DRC- and LVS-clean layout on IHP SG13G2. [docs/guide.md](docs/guide.md) shows how; [docs/rebuild.md](docs/rebuild.md) lists, tool by tool, what runs and what is still missing against Cadence.
> Everything in the window is also a command: [docs/api.md](docs/api.md).

## Parts

| Part | Where | What it does |
|---|---|---|
| **TreeSPICE** | `acutesim_engine/` (submodule, [Seshak-code/TreeSPICE](https://github.com/Seshak-code/TreeSPICE)) | The simulator: op/dc/tran/ac/noise/sens/tf/pz, Monte Carlo, 1e9-cell arrays |
| Design database | `oadb/` | Libraries (one SQLite file each), cells, views and their properties; undo/redo; text export |
| Command layer | `cmd/` | Every action a typed, validated, undoable, logged command |
| MCP server | `mcp/`, `openvlsi-mcp` | The command registry as MCP tools, for scripts and agents |
| Flow backends | `flow/` | Yosys / OpenROAD / KLayout drivers and report parsers (RTL to GDS on IHP SG13G2) |
| SoC | `soc/` | RV32I model and firmware co-simulation |

## Getting the code

```bash
git clone --recurse-submodules https://github.com/Seshak-code/OpenVLSI.git
git submodule update --init        # in an existing clone or a new worktree
```

## Building (macOS)

```bash
brew install cmake qt@6            # SQLite comes with the SDK
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(( $(sysctl -n hw.ncpu) - 2 ))
ctest --test-dir build             # the flow and SoC gates skip without ~/eda and datasets/
```

`scripts/setup_eda_tools.sh` installs the flow tools into `~/eda`. `-DOPENVLSI_GPU=ON` builds Dawn for TreeSPICE's
GPU fronts.

## Using the MCP server

```bash
./build/openvlsi-mcp --workspace ~/designs/openvlsi.libs
```

It speaks MCP over stdio. Every command is a tool: `library_create`, `cell_create`, `view_create`, `prop_set`,
`edit_undo`, and so on. A workspace file lists the libraries as `DEFINE <library> <path>` lines.

## Contributing

Make engine changes in TreeSPICE first, then bump the `acutesim_engine` submodule here. Hand-offs follow
`CLAUDE.md`, `.clinerules/` and `memory-bank/`.
