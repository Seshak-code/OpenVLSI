# GUI Orchestration Flow

The `gui/orchestration/` directory manages the connection to the external **Compute Worker**. It serves as the bridge between the Qt GUI thread and the gRPC client running in a background thread.

## Key Files & Responsibilities

### 1. Worker Manager (`worker_manager.h`)
**Role**: The Bridge. A partial-singleton that manages the lifecycle of the gRPC `WorkerClient`.
-   **Bridging**: Converts async gRPC callbacks into Qt Signals (`statusChanged`, `logReceived`).
-   **Polling**: Maintains a background thread to ping the worker health.

```cpp
struct WorkerState {
    QString workerId;
    QString version;
    bool isConnected;
    double cpuUsage;
    QString activeProvider; // e.g., "Local (Docker)"
};

class WorkerManager : public QObject {
signals:
    void statusChanged(const WorkerState& state);
    void logReceived(const QString& msg, bool isError);
    void jobFinished(bool success, const QString& resultPath);

public:
    void connectToWorker(const QString& target);
    
    // Physics Compiler Integration
    void submitPhysicsCompilation(const QString& sourcePath, const QString& outputPath);
};
```

### 2. Worker Status Panel (`worker_status_panel.h`)
**Role**: The Monitor. A UI widget (usually docked) that displays the real-time health of the backend.
-   **Visuals**: CPU utilization bar, scrolling log view for worker stdout/stderr.

```cpp
class WorkerStatusPanel : public QWidget {
private slots:
    void updateUI(const WorkerState& state);
    void appendLog(const QString& line, bool isError);
};
```

## Data Flow
1.  **Connection**: `WorkerManager::connectToWorker("localhost:50051")` is called on startup.
2.  **Polling**: The manager's background thread calls `client->GetStatus()` every 1s.
3.  **Update**: On response, it emits `statusChanged(WorkerState)`.
4.  **Rendering**: `WorkerStatusPanel` receives the signal and updates the CPU progress bar.

## Development Rules
1.  **Thread Safety**: gRPC calls happen on a background thread. NEVER touch Qt widgets directly from the gRPC callback; utilize `Q_EMIT` to pass data to the main thread.
2.  **Loose Coupling**: The GUI doesn't know *what* the worker is (Docker Container, remote cluster, WSL instance)—only that it speaks the gRPC protocol.
3.  **Strict Layering Isolation**: The GUI layer MUST NEVER directly `#include` any files from the `physics/` or `compute/solvers/` namespaces. This boundary is rigidly enforced during compilation by the `check_layering.py` pre-build hook. Any violations will break the local and CI build logic seamlessly.
