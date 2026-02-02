#include <QtTest/QtTest>
#include <QSignalSpy>
#include "gui/core/hierarchy_path.h"

using namespace acutesim::gui::core;

class TestHierarchyPath : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Basic functionality tests
    void testSetRoot();
    void testDescendInto();
    void testAscend();
    void testGoToDepth();
    void testMultipleLevels();
    void testSignalEmission();
    void testEdgeCases();

private:
    HierarchyPath* path_ = nullptr;
};

void TestHierarchyPath::initTestCase() {
    path_ = new HierarchyPath(this);
}

void TestHierarchyPath::cleanupTestCase() {
    delete path_;
    path_ = nullptr;
}

void TestHierarchyPath::testSetRoot() {
    path_->setRoot("/path/to/top.sch", "Top Design");

    QCOMPARE(path_->depth(), 0);
    QVERIFY(path_->isAtRoot());
    QCOMPARE(path_->currentFilePath(), QString("/path/to/top.sch"));
    QCOMPARE(path_->currentDisplayName(), QString("Top Design"));
    QCOMPARE(path_->levels().size(), 1);
}

void TestHierarchyPath::testDescendInto() {
    path_->setRoot("/path/to/top.sch", "Top");

    // Descend into first subcircuit
    path_->descendInto("/path/to/block1.sch", "U1", "Block1", QPointF(100, 200), 1.5);

    QCOMPARE(path_->depth(), 1);
    QVERIFY(!path_->isAtRoot());
    QCOMPARE(path_->currentFilePath(), QString("/path/to/block1.sch"));
    QCOMPARE(path_->currentDisplayName(), QString("Block1"));
    QCOMPARE(path_->levels().size(), 2);

    // Verify parent level preserved viewport state
    const HierarchyLevel& parentLevel = path_->levels().at(0);
    QCOMPARE(parentLevel.viewportCenter, QPointF(100, 200));
    QCOMPARE(parentLevel.zoomLevel, 1.5);
}

void TestHierarchyPath::testAscend() {
    path_->setRoot("/path/to/top.sch", "Top");
    path_->descendInto("/path/to/block1.sch", "U1", "Block1", QPointF(0, 0), 1.0);

    // Ascend back to root
    auto result = path_->ascend();

    QVERIFY(result.has_value());
    QCOMPARE(result->filePath, QString("/path/to/top.sch"));
    QCOMPARE(result->displayName, QString("Top"));
    QCOMPARE(path_->depth(), 0);
    QVERIFY(path_->isAtRoot());

    // Trying to ascend from root should return nullopt
    auto nullResult = path_->ascend();
    QVERIFY(!nullResult.has_value());
}

void TestHierarchyPath::testGoToDepth() {
    path_->setRoot("/path/to/top.sch", "Top");
    path_->descendInto("/path/to/block1.sch", "U1", "Block1", QPointF(0, 0), 1.0);
    path_->descendInto("/path/to/block2.sch", "U2", "Block2", QPointF(0, 0), 1.0);
    path_->descendInto("/path/to/block3.sch", "U3", "Block3", QPointF(0, 0), 1.0);

    QCOMPARE(path_->depth(), 3);

    // Go directly to depth 1
    auto result = path_->goToDepth(1);

    QVERIFY(result.has_value());
    QCOMPARE(result->displayName, QString("Block1"));
    QCOMPARE(path_->depth(), 1);
    QCOMPARE(path_->levels().size(), 2);  // Root + Block1

    // Go to root (depth 0)
    result = path_->goToDepth(0);

    QVERIFY(result.has_value());
    QCOMPARE(result->displayName, QString("Top"));
    QVERIFY(path_->isAtRoot());

    // Invalid depth should return nullopt
    auto invalid = path_->goToDepth(10);
    QVERIFY(!invalid.has_value());
}

void TestHierarchyPath::testMultipleLevels() {
    path_->setRoot("/top.sch", "Top");

    // Build a 5-level hierarchy
    for (int i = 1; i <= 4; ++i) {
        QString file = QString("/level%1.sch").arg(i);
        QString inst = QString("U%1").arg(i);
        QString name = QString("Level%1").arg(i);
        path_->descendInto(file, inst, name, QPointF(i * 10, i * 20), 1.0 + i * 0.1);
    }

    QCOMPARE(path_->depth(), 4);
    QCOMPARE(path_->levels().size(), 5);
    QCOMPARE(path_->currentDisplayName(), QString("Level4"));

    // Verify each level has correct data
    for (int i = 1; i < 5; ++i) {
        const HierarchyLevel& level = path_->levels().at(i);
        QCOMPARE(level.instanceName, QString("U%1").arg(i));
    }
}

void TestHierarchyPath::testSignalEmission() {
    HierarchyPath testPath;
    QSignalSpy levelChangedSpy(&testPath, &HierarchyPath::levelChanged);
    QSignalSpy clearedSpy(&testPath, &HierarchyPath::cleared);

    // setRoot should emit levelChanged
    testPath.setRoot("/top.sch", "Top");
    QCOMPARE(levelChangedSpy.count(), 1);

    // descendInto should emit levelChanged
    testPath.descendInto("/block.sch", "U1", "Block", QPointF(0, 0), 1.0);
    QCOMPARE(levelChangedSpy.count(), 2);

    // ascend should emit levelChanged
    testPath.ascend();
    QCOMPARE(levelChangedSpy.count(), 3);

    // clear should emit cleared
    testPath.clear();
    QCOMPARE(clearedSpy.count(), 1);
}

void TestHierarchyPath::testEdgeCases() {
    // Test empty path
    HierarchyPath emptyPath;
    QCOMPARE(emptyPath.depth(), -1);  // Empty = -1 depth
    QVERIFY(emptyPath.currentFilePath().isEmpty());
    QVERIFY(emptyPath.levels().isEmpty());

    // Ascend from empty should be safe
    auto result = emptyPath.ascend();
    QVERIFY(!result.has_value());

    // goToDepth on empty should be safe
    auto gotoResult = emptyPath.goToDepth(0);
    QVERIFY(!gotoResult.has_value());

    // Setting root on empty initializes properly
    emptyPath.setRoot("/new.sch", "New");
    QCOMPARE(emptyPath.depth(), 0);
    QVERIFY(emptyPath.isAtRoot());

    // Test resetting root clears hierarchy
    path_->setRoot("/first.sch", "First");
    path_->descendInto("/child.sch", "C1", "Child", QPointF(0, 0), 1.0);
    QCOMPARE(path_->depth(), 1);

    path_->setRoot("/second.sch", "Second");  // Should reset
    QCOMPARE(path_->depth(), 0);
    QCOMPARE(path_->currentDisplayName(), QString("Second"));
}

QTEST_MAIN(TestHierarchyPath)
#include "test_hierarchy_path.moc"
