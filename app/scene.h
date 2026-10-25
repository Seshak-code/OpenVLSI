#pragma once
// What a schematic or symbol editor draws and picks: the view's objects (from view_get, its instances placed from their
// masters' symbols) as segments, filled squares and texts in database units (y up). Each object is an Item, so
// selection and hit-testing work on what is drawn. Built through the session's read commands, as an agent sees it.
#include "app/session.h"

#include <QJsonObject>
#include <QPointF>
#include <QRectF>

#include <map>
#include <set>
#include <vector>

namespace app {

enum class Role : unsigned char { Grid, Device, Pin, Wire, Label, Annotate, Marker, Selected, Preview };

struct Seg { QPointF a, b; Role role; };
struct Dot { QPointF at; double half; Role role; };  // a filled square
struct Text { QPointF at; QString text, justify; Role role; };

struct Display {  // a frame's contents
    std::vector<Seg> segs;
    std::vector<Dot> dots;
    std::vector<Text> texts;
};

struct Item {  // one selectable object
    QString kind, key;  // as objects_move / objects_delete name it: an instance by name, the others by id
    QRectF box;
    Display geometry;
};

constexpr double kTextHeight = 40;  // database units; generated symbols put pins 200 apart

QRectF textBox(const Text& t);  // its extent, estimated from the character count

class Scene {
public:
    // "" on success, else why the view could not be read; a missing master draws as a marker, not an error
    QString load(Session& session, const QString& lib, const QString& cell, const QString& view);

    QString type() const { return view_["type"].toString(); }  // schematic or symbol
    const QJsonObject& view() const { return view_; }          // the view_get result
    const std::vector<Item>& items() const { return items_; }
    QRectF bounds() const;
    int find(const QString& kind, const QString& key) const;  // the item's index, or -1

    int hit(QPointF p, double tol) const;              // the object under p: the smallest, instances last; -1 if none
    std::vector<int> within(const QRectF& r) const;    // the objects wholly inside r
    bool connects(QPointF p) const;                    // a pin or a wire's end or segment is at p: a wire ends there
    void draw(Display& out, const std::set<int>& selected) const;

    // An instance of a master symbol at `at` (the placement preview): its geometry as an Item
    Item placed(Session& session, const QString& masterLib, const QString& masterCell, const QString& masterView,
                QPointF at, const QString& orient, const QString& name, const QJsonObject& params = {});

private:
    const QJsonObject* master(Session& session, const QString& lib, const QString& cell, const QString& view);
    QJsonObject view_;
    std::vector<Item> items_;
    std::vector<Dot> junctions_;
    std::vector<QPointF> pinPoints_;
    std::vector<std::pair<QPointF, QPointF>> wireSegs_;
    std::map<QString, QJsonObject> masters_;  // "lib/cell/view" -> its view_get (empty: missing)
};

}  // namespace app
