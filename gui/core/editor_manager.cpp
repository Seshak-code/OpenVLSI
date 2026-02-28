#include "editor_manager.h"
#include "editor_factory.h"
#include <QFileInfo>
#include "gui/widgets/inline_dialog.h"
#include <QDebug>
#include "icon_provider.h"

namespace acutesim::gui::core {

EditorManager::EditorManager(QTabWidget* tabs, QObject* parent)
    : QObject(parent), tabWidget_(tabs) {
    
    // Connect tab signals
    connect(tabWidget_, &QTabWidget::currentChanged, this, &EditorManager::onTabChanged);
    connect(tabWidget_, &QTabWidget::tabCloseRequested, this, &EditorManager::onTabCloseRequested);
}

void EditorManager::openFile(const QString& path, const QString& typeHint) {
    // 1. Check if already open
    for (int i = 0; i < tabWidget_->count(); ++i) {
        IEditor* editor = getEditor(i);
        if (editor && editor->filePath() == path) {
            tabWidget_->setCurrentIndex(i);
            return;
        }
    }

    // 2. Create new editor
    IEditor* newEditor = createEditorForFile(path, typeHint);
    if (!newEditor) {
        qWarning() << "No editor found for file:" << path;
        return;
    }

    // 3. Register and Add
    int index = tabWidget_->addTab(newEditor, newEditor->title());
    editorMap_[newEditor] = newEditor;
    tabWidget_->setCurrentIndex(index);
    
    // Connect updates
    connect(newEditor, &IEditor::contentChanged, this, &EditorManager::onEditorContentChanged);
    connect(newEditor, &IEditor::titleChanged, this, [this, newEditor](const QString&) {
        updateTabTitle(newEditor);
    });
}

IEditor* EditorManager::createEditorForFile(const QString& path, const QString& typeHint) {
    return EditorFactory::instance().createEditor(path, typeHint);
}

void EditorManager::closeEditor(int index) {
    QWidget* widget = tabWidget_->widget(index);
    IEditor* editor = getEditor(index);

    if (editor && editor->isDirty()) {
        using namespace acutesim::gui::widgets;
        InlineDialog::question(tabWidget_, "Unsaved Changes",
            QString("Save changes to %1 before closing?").arg(editor->title()),
            DialogButton::Save | DialogButton::Discard | DialogButton::Cancel,
            [this, index, widget](const DialogResult& r) {
                if (r.clickedButton == DialogButton::Cancel) return;
                if (r.clickedButton == DialogButton::Save) saveCurrent();
                doCloseEditor(index, widget);
            });
        return;
    }

    doCloseEditor(index, widget);
}

void EditorManager::doCloseEditor(int index, QWidget* widget) {
    tabWidget_->removeTab(index);
    editorMap_.remove(widget);
    delete widget;
}

void EditorManager::closeAll() {
    while (tabWidget_->count() > 0) {
        closeEditor(0);
        if (tabWidget_->count() > 0) { 
            // If count didn't decrease, user cancelled
            break; 
        }
    }
}

void EditorManager::saveCurrent() {
    // TODO: Implement save logic
}

IEditor* EditorManager::currentEditor() const {
    return getEditor(tabWidget_->currentIndex());
}

IEditor* EditorManager::getEditor(int index) const {
    QWidget* w = tabWidget_->widget(index);
    return editorMap_.value(w, nullptr);
}

void EditorManager::onTabChanged(int index) {
    IEditor* editor = getEditor(index);
    emit activeEditorChanged(editor);
    
    if (editor) {
        updateContext(editor->filePath(), editor);
    } else {
        currentContext_ = ::acutesim::orchestration::SimulationContext();
        emit contextChanged(currentContext_);
    }
}

void EditorManager::updateContext(const QString& filePath, IEditor* editor) {
    acutesim::orchestration::SimulationContext ctx = ::gui::core::SimAutoDetector::detect(filePath);
    currentContext_ = ctx;
    emit contextChanged(ctx);
}

void EditorManager::onTabCloseRequested(int index) {
    closeEditor(index);
}

void EditorManager::onEditorContentChanged() {
    IEditor* editor = qobject_cast<IEditor*>(sender());
    if (editor) updateTabTitle(editor);
}

void EditorManager::updateTabTitle(IEditor* editor) {
    int index = tabWidget_->indexOf(editor);
    if (index >= 0) {
        QString txt = editor->title();
        if (editor->isDirty()) txt += " •";
        tabWidget_->setTabText(index, txt);
        
        // Update Icon based on file type (via EditorFactory registry)
        QFileInfo fi(editor->filePath());
        QString ext = fi.suffix().toLower();
        QString iconName = EditorFactory::instance().iconForExtension(ext);
        tabWidget_->setTabIcon(index, IconProvider::icon(iconName));
    }
}

} // namespace acutesim::gui::core
