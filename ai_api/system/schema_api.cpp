#include "schema_api.h"
#include "gui/core/mainwindow.h"
#include "gui/editors/schematic/schematic_editor.h"
#include <QApplication>

namespace AIAPI {

static SchematicEditor* getEditor() {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (MainWindow* mw = qobject_cast<MainWindow*>(widget)) {
            return mw->getSchematicEditor();
        }
    }
    return nullptr;
}

QJsonObject getFullCircuitState() {
     SchematicEditor* editor = getEditor();
     if (!editor) return QJsonObject();
     return editor->getCircuitState();
}

} // namespace AIAPI
