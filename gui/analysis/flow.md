# GUI Analysis Flow

The `gui/analysis/` directory contains the **Scientific Instruments** of the application. These components are responsible for orchestrating simulations, validating circuit rules, and visualizing complex datasets.

## 1. Electrical Rule Checking (ERC)

### ERC Engine (`erc_engine.h`)
**Role**: Real-time background validation of the schematic.
-   **Checks**: Floating nodes, short circuits, mode violations (Block/System).

```cpp
enum class ViolationType {
    NO_GROUND, FLOATING_NODE, SHORT_CIRCUIT,
    NEAR_MISS, MULTIPLE_DRIVERS,
    ILLEGAL_SOURCE, MISSING_PORTS // Mode-specific
};

class ERCEngine {
public:
    QList<Violation> runFullCheck(QGraphicsScene* scene);
    void setMode(AppMode mode);
    bool hasBlockingErrors() const;
};
```

### ERC AutoFix (`erc_autofix.h`)
**Role**: Intelligent routing engine to suggest fixes for violations.
-   **Algorithm**: A* Pathfinding with collision avoidance.

```cpp
class ERCAutoFix {
public:
    // Suggest a routing fix for a violation
    RoutingHint suggestFix(const Violation& violation, 
                           QGraphicsScene* scene, 
                           const QRectF& dieArea = QRectF());
    
    // Visualize the suggestion as a "ghost" wire
    void showHint(QGraphicsScene* scene, const RoutingHint& hint);
};
```

## 2. Simulation Environment

### Simulation Workspace (`simulation_workspace.h`)
**Role**: The main "Laboratory". Dockable environment managing profiles, runs, and results.
-   **VS Code Style**: Explorer on left, Editor in center, Results at bottom.

```cpp
class SimulationWorkspace : public QWidget {
public:
    void selectProfile(const QString& profileId);
    void selectComputeEngine(CircuitSim::ExecMode mode); // Legacy, SoA, SIMD, GPU (Dawn)
    void onTopologyChanged(size_t newHash);
    
public slots:
    void runCurrent();
    void runQuickDC();
    void configureCorners();
};
```

### Simulation Explorer (`simulation_explorer.h`)
**Role**: Tree view for profiles and run history.

```cpp
class SimulationExplorer : public QWidget {
    void addProfile(const Simulation::SimulationProfile& profile);
    void addRun(const Simulation::SimulationRun& run);
    
signals:
    void profileSelected(const QString& profileId);
    void runDiffRequested(const QString& runId1, const QString& runId2);
};
```

### System Inspector (`system_inspector.h`)
**Role**: Controls environmental variables for the simulation (Temp, VDD, Noise).

```cpp
struct State {
    double vdd_voltage = 1.8;
    double temp_C = 25.0;
    bool thermal_noise = true;
    bool high_precision = true; // Dual-Scale Acc
};

State getState() const;
void applyState(const State& s);
```

## 3. Signal Analysis

### Logic Waveform Widget (`logic_analyzer.h`)
**Role**: Digital logic visualizer (Logic Analyzer).
-   **Theme**: Dark mode, bus support.

```cpp
struct WaveformTrack {
    QString name;
    bool isBus;
    int bitWidth;
    QVector<Sample> history;
};

class LogicWaveformWidget : public QWidget {
    void addBusData(const std::string& name, 
                    const std::vector<BusProbe::DigitalSample>& history, 
                    int bitWidth);
};
```

## 4. Statistical Analysis

### Statistical Dashboard (`statistical_dashboard.h`)
**Role**: Visualizes yield and sensitivity.
-   **Yield**: Histogram of Monte Carlo results against spec.
-   **Sensitivity**: Pareto chart of parameter impact.

```cpp
class YieldHistogramWidget : public QWidget {
    void setData(const std::vector<double>& results, 
                 double specMin, double specMax);
};

class SensitivityParetoWidget : public QWidget {
    void setData(const std::vector<SensitivityDisplayItem>& items);
};
```

### Monte Carlo Setup (`monte_carlo_setup_dialog.h`)
**Role**: Configuration dialog for statistical runs.

```cpp
class MonteCarloSetupDialog : public QDialog {
    int numRuns() const;
    Statistics::SamplingStrategy strategy() const;
    std::map<std::string, Statistics::Distribution> variations() const;
};
```

### Corner Setup (`corner_setup_widget.h`)
**Role**: Configuration for PVT (Process, Voltage, Temperature) corners.

```cpp
// Returns the list of corners to run
QVector<acutesim::physics::CornerConfig> getCorners() const;
```

## Development Rules
1.  **Engine Separation**: GUI classes (`Workspace`, `Explorer`) simply orchestrate the underlying physics engines (`CircuitSim`, `ERCEngine`). They do not perform calculations themselves.
2.  **Mode Awareness**: The `ERCEngine` must be aware of the current `AppMode` (Bench/Block/System) to apply the correct rule set.
3.  **Visualization Decoupling**: Visualizers (`LogicWaveformWidget`) ingest raw data copies, decoupling rendering frame rate from simulation step rate.
4.  **Robust Input Handling**: Input parameters (time strings, frequencies) must be parsed via the centralized `acutesim::utils::parseUnitValue` helper to ensure consistency (e.g., "1.2ms", "5GHz").
