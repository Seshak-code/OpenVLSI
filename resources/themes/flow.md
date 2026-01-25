# Themes Resource Flow

The `resources/themes/` directory contains the **Visual Identity Definitions** for CuteApp. It allows for data-driven styling, ensuring the UI can adapt to user preferences (Dark/Light/High Contrast) without recompilation.

The themes are consumed by the `ThemeManager` in `gui/styling/`, which acts as the single source of truth for all UI colors and fonts.

## Key Files & Structure

### 1. Theme Files (`*.json`)
Each JSON file defines a complete or partial color palette.
*   **Format**: Standard JSON.
*   **Inheritance**: Themes can use `"extends": "ReferenceTheme"` to inherit base colors and only override specifics.
*   **Hot Reload**: The `ThemeManager` watches these files. modifying a theme while the app is running updates the UI instantly.

**JSON Schema:**
```json
{
    "name": "Dark Modern",
    "version": 1,
    "type": "dark", 
    "extends": "common_base", // Optional inheritance
    "colors": {
        "ui.background": "#1e1e1e",
        "ui.text": "#cccccc",
        "schematic.wire": "#ffffff"
    }
}
```

### 2. `gui/styling/theme_manager.cpp` (The Consumer)
This C++ class loads the JSON, resolves inheritance, and validates accessibility.
*   **Token Registry**: Explicitly defines all valid keys (e.g., `ui.accent`) and their default fallback values.
*   **Contrast Validation**: Automatically checks WCAG AA/AAA contrast ratios (e.g., text vs background) and logs warnings if a theme is inaccessible.
*   **Application**: Generates a global Qt Stylesheet (`QString getStyleSheet()`) for widgets and provides programmatic access (`QColor getColor()`) for custom painting (e.g., in `SchematicEditor`).

## Core Logic & Snippets

### Token Registry (`theme_manager.cpp`)
All valid keys are defined here. Adding a new color token requires updating this registry.

```cpp
void ThemeManager::initTokenRegistry() {
    // Defines key, default color, required flag, and description
    auto reg = [&](const QString& key, const QColor& def, bool req, const QString& desc) { ... };

    reg("ui.background", QColor("#1e1e1e"), true, "Global background");
    reg("ui.text", QColor("#cccccc"), true, "Primary text color");
    reg("schematic.grid", QColor(60, 60, 60), true, "Grid dots/lines");
}
```

### Hot Reload (`theme_manager.cpp`)
Enables rapid iteration on visual design.

```cpp
void ThemeManager::onThemeFileChanged(const QString& path) {
    if (path == currentThemePath_) {
        qDebug() << "Hot Reloading Theme:" << path;
        loadThemeFromJson(path);
        // Triggers repaint across the application
        emit themeChanged();
    }
}
```

### Accessibility Check (`theme_manager.cpp`)
Ensures themes are usable.

```cpp
void ThemeManager::validateActiveTheme() {
    QColor bg = getColor("ui.background");
    QColor text = getColor("ui.text");
    
    // Check Contrast Ratio (e.g. 4.5:1 for AA)
    if (!validateContrast(text, bg, false /* strictAAA */)) {
        qWarning() << "Accessibility Warning: Low contrast (" << getContrastRatio(text, bg) << ":1)";
    }
}
```

## Development Rules

1.  **No Hardcoded Colors**: Never use `Qt::red` or `QColor("#333")` directly in widget code. Always use `ThemeManager::instance().getColor("token.name")`.
2.  **define in Registry First**: Before using a new token like `sidebar.icon`, define it in `initTokenRegistry` with a sensible default.
3.  **Validate Contrast**: When creating a new theme, check the application logs for "Accessibility Warning".
4.  **Keep it Flat**: While nesting in JSON is parsed, essentially the structure is a flat map of `string -> color`. Use dot notation (`ui.background`) for hierarchy.
5.  **Schematic separation**: `schematic.*` tokens are distinct from `ui.*` tokens because the canvas might be dark even in a light theme (or vice versa).
