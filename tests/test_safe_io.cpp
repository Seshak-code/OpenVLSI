#include <QtTest>
#include "../ai_api/context/safe_io.h"
#include <QTemporaryFile>
#include <QDir>

class TestSafeIo : public QObject {
    Q_OBJECT

private slots:
    void testBoundedRead_data();
    void testBoundedRead();
    void testMalformedJson();
    void testAtomicWrite();
};

void TestSafeIo::testBoundedRead_data() {
    QTest::addColumn<int>("fileSize");
    QTest::addColumn<qint64>("limit");
    QTest::addColumn<bool>("shouldPass");

    QTest::newRow("Small File Pass") << 1024 << SafeIo::MAX_CONFIG_SIZE << true;
    QTest::newRow("Large File Fail") << (2 * 1024 * 1024) << SafeIo::MAX_CONFIG_SIZE << false; // 2MB > 1MB
    QTest::newRow("Exact Limit Pass") << (1 * 1024 * 1024) << SafeIo::MAX_CONFIG_SIZE << true;
}

void TestSafeIo::testBoundedRead() {
    QFETCH(int, fileSize);
    QFETCH(qint64, limit);
    QFETCH(bool, shouldPass);

    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    
    // Create dummy data
    QByteArray data(fileSize, 'A');
    tempFile.write(data);
    tempFile.close(); 
    // Note: QTemporaryFile usually deletes on destruction, but checking exists() handles if it somehow fails
    // However, it stays on disk as long as the object exists.

    SafeIo::ReadResult result = SafeIo::read_file_bounded(tempFile.fileName(), limit);

    if (shouldPass) {
        QVERIFY2(result.success, qPrintable(result.errorMessage));
        QCOMPARE(result.data.size(), fileSize);
    } else {
        QVERIFY(!result.success);
        QVERIFY(result.errorMessage.contains("exceeds size limit"));
    }
}

void TestSafeIo::testMalformedJson() {
    QTemporaryFile tempFile;
    QVERIFY(tempFile.open());
    tempFile.write("{ \"key\": \"value\", "); // Missing closing brace
    tempFile.close();

    SafeIo::JsonReadResult result = SafeIo::read_json_file_bounded(tempFile.fileName());
    
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("JSON parse error"));
}

void TestSafeIo::testAtomicWrite() {
    // We can't easily test atomicity (crash during write) without mocking system calls.
    // Instead we test that the basic write cycle works correctly.
    
    QString path = QDir::tempPath() + "/safe_io_test_atomic.json";
    QJsonObject obj;
    obj["status"] = "ok";
    obj["value"] = 42;
    
    SafeIo::WriteResult result = SafeIo::write_json_safe(path, obj);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    
    QFile file(path);
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QCOMPARE(doc.object()["value"].toInt(), 42);
    
    file.remove();
}

QTEST_MAIN(TestSafeIo)
#include "test_safe_io.moc"
