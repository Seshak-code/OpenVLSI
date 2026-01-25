# GUI Styling Flow

The `gui/styling/` directory manages the application's visual appearance through a token-based theming engine. It ensures consistency and accessibility across all widgets.

## Key Files & Responsibilities

### 1. Theme Manager (`theme_manager.h`)
**Role**: The Token Registry. Stores color and font definitions for the active theme (Dark/Light/High Contrast).
-   **Tokens**: Uses semantic names like `ui.background` instead of hardcoded hex values.
-   **Validation**: Enforces WCAG contrast ratios to ensure readability.

```cpp
struct ThemeToken {
    QString key;
    QColor defaultValue;
    bool required;
    QString description;
};

class ThemeManager : public QObject {
public:
    static ThemeManager& instance();

    // Accessors
    QColor getColor(const QString& token) const;
    QFont getFont(const QString& token = "ui.font") const;
    
    // Accessibility & Validation
    static double getContrastRatio(const QColor& c1, const QColor& c2);
    static bool validateContrast(const QColor& c1, const QColor& c2, bool strictAAA = false);
};
```

## Data Flow
1.  **Load**: `ThemeManager::loadTheme("Dark")` reads a JSON definition.
2.  **Access**: A widget calls `ThemeManager::instance().getColor("ui.accent")` in its `paintEvent`.
3.  **Hot Reload**: If the theme JSON changes on disk, `fileWatcher` triggers a reload and emits `themeChanged()`.
4.  **Repaint**: Widgets connect to `themeChanged()` and trigger `update()`.

## Development Rules
1.  **No Hardcoded Colors**: NEVER use `Qt::red` or `"#FF0000"`. Always request a semantic token (e.g., `error()`).
2.  **Contrast Checks**: New tokens must pass `validateContrast()` against their intended background.
