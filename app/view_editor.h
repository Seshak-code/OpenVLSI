#pragma once
// The schematic and symbol editors (Virtuoso's): one window per view, its tools on Virtuoso's bindkeys. A tool turns
// clicks into commands (instance_add, wire_add, objects_move, ...) through the session, so each edit is one undo step,
// echoed in the CIW, and the same edit an agent could make. The editor redraws from the database after every change,
// its own or anyone's (ponytail: by reloading the whole view; per-object updates when views get large).
//
// The tools take points in database units (press / moveTo / release / doubleClick): the canvas feeds them from the
// mouse, the tests directly. Points snap to the grid.
#include "app/scene.h"

#include <QMainWindow>

#include <set>

class QLabel;

namespace app {

class Canvas;

class ViewEditor : public QMainWindow {
    Q_OBJECT
public:
    enum class Tool { Select, Move, Instance, Wire, Label, Pin, Line, Rect, Polygon, Ellipse, Text };
    static constexpr double kSnap = 25;  // database units

    ViewEditor(Session& session, const QString& lib, const QString& cell, const QString& view, QWidget* parent = nullptr);
    QString library() const { return lib_; }
    QString cell() const { return cell_; }
    QString view() const { return view_; }
    bool isSchematic() const { return scene_.type() == "schematic"; }
    const Scene& scene() const { return scene_; }
    Canvas* canvas() const { return canvas_; }
    Tool tool() const { return tool_; }
    QString prompt() const;

    // the tools
    void escape();  // ends the tool in progress (a wire or line with enough points is kept); back to selecting
    void startInstance(const QString& lib, const QString& cell, const QString& view = "symbol");
    void startWire();
    void startLabel(const QString& names);  // one label per click, in order
    void startPin(const QString& names, const QString& direction);
    void startShape(Tool kind);             // Line, Rect, Polygon, Ellipse
    void startText(const QString& text);
    void startMove();  // the selection: a reference point, then where it goes
    void rotate();     // placing: the next orientation; selecting: the one selected instance, by 90°
    void press(QPointF world, Qt::KeyboardModifiers modifiers = {});
    void moveTo(QPointF world);
    void release(QPointF world);
    void doubleClick(QPointF world);

    std::set<std::pair<QString, QString>> selection() const { return selection_; }  // (kind, key)
    void deleteSelection();
    bool undo();
    bool redo();
    QJsonObject check();    // schematic_check
    bool generateSymbol();  // symbol_generate from this schematic, then opens it
    bool descend();         // opens the selected instance's master (its schematic if it has one)
    int selectNet(const QString& net);  // a schematic's wires and labels of that net, selected; how many
    QStringList plotSelectedNets();     // Plot Net: each selected wire's or label's net, probed; the nets
    void fit();

public slots:
    void reload();

private:
    QPointF snap(QPointF p) const;
    double tolerance() const;
    cmd::Result call(const QString& name, QJsonObject args);  // on this view
    void setTool(Tool t);
    void finishPath();  // a wire, line or polygon in progress
    void addRouted(QPointF p);
    void redraw();
    void editProperties();
    void buildMenus();

    Session& session_;
    QString lib_, cell_, view_;
    Scene scene_;
    Canvas* canvas_;
    QLabel* status_;
    Tool tool_ = Tool::Select;
    std::set<std::pair<QString, QString>> selection_;
    std::vector<QPointF> points_;  // the path or corner in progress
    QPointF cursor_;
    QStringList names_;            // labels, pins or a text still to place
    QString direction_, masterLib_, masterCell_, masterView_, orient_ = "R0";
    bool pressing_ = false, dragged_ = false, boxing_ = false, refSet_ = false;
    QPointF pressAt_, ref_;
};

}  // namespace app
