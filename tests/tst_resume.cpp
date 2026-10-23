#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include "ui/ResumeUploadWidget.h"
#include <QLabel>
#include <QPushButton>

class ResumeTest : public QObject {
    Q_OBJECT
    QJsonObject read(const QString &file) {
        QProcess reader;
        reader.start(QCoreApplication::applicationDirPath() + "/ResumeReader.exe", {file});
        if (!reader.waitForStarted(3000))
            return {{"error", reader.errorString()}};
        if (!reader.waitForFinished(30000)) {
            reader.kill();
            reader.waitForFinished();
            return {{"error", "reader timed out"}};
        }
        return QJsonDocument::fromJson(reader.readAllStandardOutput()).object();
    }
    QString write(QTemporaryDir &dir, const QString &name, const QByteArray &bytes) {
        const auto path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            return {};
        return path;
    }
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QApplication::setFont(QFont("Microsoft YaHei", 9));
    }
    void extractsUnicodeTextAndKeepsOnlyFileName() {
        QTemporaryDir dir;
        const auto text = QStringLiteral("张三\n嵌入式软件工程师\nC++、Qt、FreeRTOS 项目经验");
        const auto path = write(dir, QStringLiteral("我的简历.txt"), text.toUtf8());
        const auto result = read(path);
        QVERIFY2(!result.contains("error"), qPrintable(result["error"].toString()));
        QCOMPARE(result["text"].toString(), text);
        QCOMPARE(result["fileName"].toString(), QStringLiteral("我的简历.txt"));
        QCOMPARE(
            result["sha256"].toString(),
            QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex()));
        QVERIFY(!QString::fromUtf8(QJsonDocument(result).toJson()).contains(dir.path()));
    }
    void failedImportKeepsPreviousResumeAndReadOnlyPreventsChanges() {
        QTemporaryDir dir;
        ResumeUploadWidget widget;
        const QJsonObject original{{"fileName", "previous.txt"}, {"text", "Original resume"}};
        widget.setDocument(original);
        QSignalSpy changes(&widget, &ResumeUploadWidget::documentChanged);
        widget.importFile(write(dir, "broken.pdf", "broken PDF"));
        QVERIFY(widget.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!widget.busy(), 5000);
        QCOMPARE(widget.document(), original);
        QVERIFY(changes.isEmpty());
        QVERIFY(!widget.findChild<QLabel *>("resumeStatus")->text().isEmpty());
        widget.setReadOnly(true);
        widget.importFile(write(dir, "valid.txt", "New resume"));
        QVERIFY(!widget.busy());
        QVERIFY(!widget.findChild<QPushButton *>("removeResumeButton")->isEnabled());
        QCOMPARE(widget.document(), original);
        widget.setReadOnly(false);
        widget.importFile(dir.filePath("valid.txt"));
        QTRY_VERIFY_WITH_TIMEOUT(!widget.busy(), 5000);
        QCOMPARE(widget.document()["text"].toString(), QString("New resume"));
        QCOMPARE(changes.size(), 1);
        widget.findChild<QPushButton *>("removeResumeButton")->click();
        QVERIFY(widget.document().isEmpty());
        QCOMPARE(changes.size(), 2);
    }
    void extractsDocxParagraphsTablesAndHeaders() {
        const auto path = QFINDTESTDATA("fixtures/resume/candidate.docx");
        QVERIFY(!path.isEmpty());
        const auto result = read(path);
        QVERIFY2(!result.contains("error"), qPrintable(result["error"].toString()));
        const auto text = result["text"].toString();
        QVERIFY(text.contains(QStringLiteral("候选人：张三")));
        QVERIFY(text.contains("FreeRTOS"));
        QVERIFY(text.contains(QStringLiteral("雷达信号处理")));
        QVERIFY(text.contains("C6678"));
        QVERIFY(!text.contains("<w:"));
    }
    void readsUtf16AndGb18030Text() {
        QTemporaryDir dir;
        auto result = read(write(dir, "utf16.txt", QByteArray::fromHex("fffe807b8653200051007400")));
        QVERIFY2(!result.contains("error"), qPrintable(result["error"].toString()));
        QCOMPARE(result["text"].toString(), QStringLiteral("简历 Qt"));
        result = read(write(dir, "legacy-suffix.txt", QByteArray("Resume ") + QByteArray::fromHex("e4b8")));
        QVERIFY2(!result.contains("error"), qPrintable(result["error"].toString()));
        QCOMPARE(result["text"].toString(), QStringLiteral("Resume 涓"));
        result = read(write(dir, "gb18030.txt", QByteArray::fromHex("bcf2c0fa205174")));
        QVERIFY2(!result.contains("error"), qPrintable(result["error"].toString()));
        QCOMPARE(result["text"].toString(), QStringLiteral("简历 Qt"));
    }
    void rejectsBrokenDeclaredEncodingWithoutReplacingResume_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("utf8-bom-truncated") << QByteArray::fromHex("efbbbf526573756d6520e4b8");
        QTest::newRow("utf16le-odd-byte") << QByteArray::fromHex("fffe52006500730075006d006500200080");
        QTest::newRow("utf16le-high-surrogate")
            << QByteArray::fromHex("fffe52006500730075006d006500200000d8");
        QTest::newRow("utf16be-low-surrogate") << QByteArray::fromHex("feff0052006500730075006d00650020dc00");
    }
    void rejectsBrokenDeclaredEncodingWithoutReplacingResume() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir dir;
        const auto path = write(dir, "broken-encoding.txt", bytes);
        const auto result = read(path);
        QVERIFY2(!result["error"].toString().isEmpty(),
                 "A declared encoding must not silently lose trailing bytes");
        QVERIFY(result["text"].toString().isEmpty());
        ResumeUploadWidget widget;
        const QJsonObject previous{{"fileName", "previous.txt"}, {"text", "Previous candidate background"}};
        widget.setDocument(previous);
        widget.importFile(path);
        QTRY_VERIFY_WITH_TIMEOUT(!widget.busy(), 5000);
        QCOMPARE(widget.document(), previous);
    }
    void extractsPdfAcrossPages() {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("双页简历.pdf"));
        {
            QPdfWriter writer(path);
            writer.setResolution(96);
            QPainter painter(&writer);
            painter.setFont(QFont("Microsoft YaHei", 14));
            painter.drawText(QPointF(60, 70), QStringLiteral("张三 C++ Qt FreeRTOS"));
            writer.newPage();
            painter.drawText(QPointF(60, 70), "Radar project C6678 RESUME-END");
        }
        const auto result = read(path);
        QVERIFY2(!result.contains("error"), qPrintable(result["error"].toString()));
        QVERIFY(result["text"].toString().contains(QStringLiteral("张三")));
        QVERIFY(result["text"].toString().contains("FreeRTOS"));
        QVERIFY(result["text"].toString().contains("RESUME-END"));
    }
    void rejectsUnreadableAndUnsupportedFiles_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("bad-pdf") << QString("bad.pdf") << QByteArray("not a PDF");
        QTest::newRow("bad-docx") << QString("bad.docx") << QByteArray("not a ZIP");
        QTest::newRow("old-doc") << QString("old.doc") << QByteArray("old binary");
        QTest::newRow("binary-txt") << QString("binary.txt") << QByteArray::fromHex("000102030405");
        QTest::newRow("empty-txt") << QString("empty.txt") << QByteArray(" \n\t");
    }
    void rejectsUnreadableAndUnsupportedFiles() {
        QFETCH(QString, name);
        QFETCH(QByteArray, bytes);
        QTemporaryDir dir;
        const auto result = read(write(dir, name, bytes));
        QVERIFY(!result["error"].toString().isEmpty());
        QVERIFY(result["text"].toString().isEmpty());
    }
    void rejectsBlankPdfRatherThanImportingEmptyResume() {
        QTemporaryDir dir;
        const auto path = dir.filePath("blank.pdf");
        {
            QPdfWriter writer(path);
            QPainter painter(&writer);
            painter.fillRect(20, 20, 100, 100, Qt::black);
        }
        const auto result = read(path);
        QVERIFY(result["error"].toString().contains(QStringLiteral("文字")));
        QVERIFY(result["text"].toString().isEmpty());
    }
    void rejectsOversizeTextRatherThanSilentlyTruncating() {
        QTemporaryDir dir;
        auto result = read(write(dir, "too-long.txt", QByteArray(20001, 'x')));
        QVERIFY(!result["error"].toString().isEmpty());
        QVERIFY(result["text"].toString().isEmpty());
        QFile file(dir.filePath("too-large.txt"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(21 * 1024 * 1024));
        file.close();
        result = read(file.fileName());
        QVERIFY(!result["error"].toString().isEmpty());
    }
};
QTEST_MAIN(ResumeTest)
#include "tst_resume.moc"
