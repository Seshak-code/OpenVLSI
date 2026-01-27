# Components Module Flow

The `components/` directory is the **Structural Truth** of AcuteSim. It defines *what* the circuit is, independent of *how* it is simulated (Physics) or *how* it is differentiated (Autodiff).

It serves as the bridge between:
1.  **The User/AI**: Who think in terms of "Resistors", "Modules", and "Netlists".
2.  **The Solver**: Which thinks in terms of "Matrix Stamps" and "Equation Pointers".
3.  **The Library**: Which needs to save/load designs and ensure version control.

## Key Files & Responsibilities

### 1. `circuit.h`: The Structural Core (`TensorBlock` & `BlockState`)
Defines the `TensorBlock`, which is the container for a netlist.
*   **TensorBlock**: Holds *immutable definition* (Resistors, Capacitors, Model Params). Can be reused across multiple instances.
*   **BlockState**: Holds *mutable simulation state* (History buffers, Operating points). Unique per instance.
*   **TensorNetlist**: The root object. Manages `globalBlock` + `globalState` and a list of `instances` + `instanceStates`.

### 2. `graph_canonical.h`: The Identity Engine
Implements **Orientation-Invariant Graph Hashing**.
*   **Problem**: Netlist `A-B-C` is physically identical to `C-B-A`, but a naive diff would see them as different.
*   **Solution**: `computeTopologyHash` uses a BFS-based canonical relabeling (starting from Ground) to generate a hash that is invariant to node numbering order or component list order.
*   **Rule**: If `hash(A) == hash(B)`, then A and B are isomorphic interactively.

### 3. `netlist_diff.h`: The Change Detector
Implements **Semantic Diffing**.
*   Instead of diffing text lines, it diffs component functionality.
*   **Categories**:
    *   `ADDED`/`REMOVED`: Structural change.
    *   `MODIFIED_PARAMETER`: Value change (e.g., 1k -> 2k).
    *   `CONNECTIVITY_CHANGED`: Same component, different wiring.
*   **Impact Analysis**: Returns a `SimImpact` score. Changing a label is `NONE`. Changing a Resistor is `MEDIUM`. Changing Connectivity is `HIGH` (Full Re-solve).

### 4. `standard_cells.h`: The Standard Library
Defines the **PDK-compliant** hardcoded components (e.g., Darlington Pairs, OpAmps).
*   **Source of Truth**: These are C++ definitions, not JSON. They ensure rigorous physics matching (e.g., `verify_bjt_thermal` constants).
*   **Abstractions**:
    * `SubCircuitDefinition`: A template `TensorBlock` with exposed pins.
    * **Hierarchical Mapping**: `addInstance` automates the allocation of internal nodes to global equations during netlist assembly.
    * **Standard PDK Cells**: `PDK_AND2`, `PDK_OR2`, `PDK_XOR2`, `PDK_XNOR2`, `PDK_MUX2`, `PDK_Buffer`, `PDK_DFF`.

### 5. `component_library.h`: Persistence & AI Interface
Handles JSON serialization/deserialization for *User-Defined* components.
*   **Format**: `.comp` files (JSON).
*   **AI Metadata**: Stores "Description", "Tags", and "LogicSummary" to help the AI understand what the component *does*.

## Core Logic & Snippets

### Orientation-Invariant Hashing (`graph_canonical.h`)
This ensures we don't re-simulate identical topologies just because the user dragged a wire.

```cpp
inline uint64_t computeTopologyHash(std::vector<CanonicalEdge> edges, int groundNode = 0) {
    // 1. Normalize Ground to Node 0
    normalizeGround(edges, groundNode);
    
    // 2. Canonical Relabeling (BFS from Ground)
    // Renames nodes based on distance from ground and connectivity "weight"
    canonicalRelabel(edges);
    
    // 3. Sort Edges
    // Ensures list order doesn't affect hash
    std::sort(edges.begin(), edges.end());
    
    // 4. Hash (FNV-1a)
    uint64_t hash = 14695981039346656037ULL;
    for (const auto& e : edges) {
        hash = fnv1a(hash, e.u);
        hash = fnv1a(hash, e.v);
        hash = fnv1a(hash, e.type);
    }
    return hash;
}
```

### Semantic Diffing (`netlist_diff.h`)
Used to determine if the "Speculative Runner" needs to fire.

```cpp
template<typename T>
static void diffComponents(const std::vector<T>& oldList, 
                           const std::vector<T>& newList,
                           DiffReport& report) 
{
    // Map components by Unique ID (Name)
    auto oldMap = mapByName(oldList);
    auto newMap = mapByName(newList);

    for (const auto& [name, newComp] : newMap) {
        if (oldMap.find(name) == oldMap.end()) {
            report.addChange(name, ChangeType::ADDED, SimImpact::HIGH);
        } else {
            const auto& oldComp = oldMap[name];
            if (oldComp.nodes != newComp.nodes) {
                 report.addChange(name, ChangeType::CONNECTIVITY_CHANGED, SimImpact::HIGH);
            } else if (oldComp.value != newComp.value) {
                 report.addChange(name, ChangeType::MODIFIED_PARAMETER, SimImpact::MEDIUM);
            }
        }
    }
}
```

### Component Serialization (`component_library.h`)
Saves the component in a format readable by both humans and LLMs.

```cpp
inline bool saveComponent(const std::string& filepath, const LibraryComponent& comp) {
    std::ofstream file(filepath);
    file << "{\n";
    file << "  \"name\": \"" << comp.name << "\",\n";
    file << "  \"ai_description\": \"" << comp.aiDescription << "\",\n"; // Context for AI
    file << "  \"topology_hash\": " << comp.topologyHash << ",\n";
    file << "  \"devices\": {\n";
    file << "    \"resistors\": " << ComponentJSON::serializeResistors(comp.block.resistors) << "\n";
    file << "  }\n";
    file << "}\n";
    return file.good();
}
```

## Development Rules

1.  **Topology is King**: Any change to `circuit.h` structures MUST be reflected in `graph_canonical.h`. If two structurally identical circuits produce different hashes, the cache breaks.
2.  **Semantic over Syntactic**: When implementing diff logic, care about *physics* changes, not *metadata* changes. A description change should not trigger a solve.
3.  **Ground Zero**: Always assume Node 0 is global ground for canonicalization purposes.
4.  **AI-First Persistence**: When saving components, always preserve fields that help the AI (descriptions, logic summaries). Don't strip "unknown" fields if possible.
## 🤖 SME Validation Checklist
*(Consult this list before modifying `components/`)*

- [ ] **Hashing**: If you modified `TensorBlock` or `CircuitNode`, did you update `graph_canonical.h` to include the new field in the hash?
- [ ] **Semantic Diff**: Did you update `netlist_diff.h` to detect changes in this new field?
- [ ] **Persistence**: Did you update `component_library.h` JSON serialization/deserialization?
- [ ] **AI Metadata**: Is `aiDescription` and `tags` preserved during all transformations?
- [ ] **Ground Zero**: Does the new component respect Node 0 as global ground?
