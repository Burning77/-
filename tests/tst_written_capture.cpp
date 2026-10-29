#include "capture/RegionSelector.h"
#include "capture/SelectionCopy.h"
#include "capture/WrittenInput.h"
#include "capture/ScreenshotStore.h"
#include "storage/SessionStore.h"
#include "ui/CapturePrivacy.h"
#include <QBuffer>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QFontDatabase>
#include <QPainter>
#include <QtTest>
class WrittenCaptureTest : public QObject {
    Q_OBJECT
    QTemporaryDir profile{QDir::currentPath() + "/written-capture-XXXXXX"};
  private slots:
    void initTestCase() {
        QVERIFY(profile.isValid());
        QCoreApplication::setOrganizationName("InterviewCaptureTests");
        QCoreApplication::setApplicationName("WrittenCapture");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
        QStandardPaths::setTestModeEnabled(true);
    }
    void modeKeysReleaseAndSelectionFlowsToQuestion() {
        QWidget owner;
        QSet<int> registered;
        quint64 revision = 1;
        SelectionBackend selected;
        selected.foreground = [] { return quintptr(123); };
        selected.external = [](quintptr) { return true; };
        selected.released = [] { return true; };
        selected.clipboardRevision = [&] { return revision; };
        selected.clipboardMatchesTarget = [](quintptr) { return true; };
        selected.copy = [&] {
            ++revision;
            return true;
        };
        selected.readText = [](quint64, SelectionTextCallback done) { done("Explain SPI mode 0", {}); };
        WrittenInput input(
            &owner,
            [&](int id, quint32, quint32, bool on) {
                if (on)
                    registered.insert(id);
                else
                    registered.remove(id);
                return HotkeyResult{true, {}};
            },
            selected);
        QSignalSpy result(&input, &WrittenInput::questionCaptured);
        input.start();
        QCOMPARE(registered.size(), 2);
        input.captureSelection();
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(result[0][0].toString(), QString("Explain SPI mode 0"));
        input.stop();
        QVERIFY(registered.isEmpty());
        input.captureSelection();
        QTest::qWait(80);
        QCOMPARE(result.size(), 1);
    }
    void screenshotAssetIsExactAndRemovedWithSession() {
        SessionStore store;
        const auto root = profile.path() + "/image-session";
        QVERIFY(store.open(root));
        QVERIFY(store.put("sessions", {{"id", "session"}}));
        QImage image(20, 15, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QByteArray png;
        QBuffer output(&png);
        output.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&output, "PNG"));
        QString error;
        auto metadata = ScreenshotStore::save(root, "session", "question", png, &error);
        QVERIFY2(!metadata.isEmpty(), qPrintable(error));
        QCOMPARE(ScreenshotStore::read(root, "session", "question", metadata, &error), png);
        const auto file = root + "/" + metadata["path"].toString();
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(ScreenshotStore::read(root, "other-session", "question", metadata, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QFile tampered(file);
        QVERIFY(tampered.open(QIODevice::WriteOnly));
        tampered.write("changed");
        tampered.close();
        QVERIFY(ScreenshotStore::read(root, "session", "question", metadata, &error).isEmpty());
        QVERIFY(store.removeSession("session"));
        QVERIFY(!QFileInfo::exists(file));
    }
    void screenshotPipelineKeepsPixelsAndCancelsLateCapture() {
        QWidget owner;
        QImage original(150, 90, QImage::Format_RGB32);
        original.fill(QColor("#ff0088"));
        int grabs = 0;
        WrittenInput input(
            &owner, [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; }, {},
            [](quintptr, bool) { return WindowCaptureResult{true, {}}; },
            [&](QScreen *, QRect) {
                ++grabs;
                return original;
            });
        QSignalSpy images(&input, &WrittenInput::imageCaptured);
        input.start();
        input.captureRegion();
        auto selector = owner.findChild<RegionSelector *>();
        QVERIFY(selector);
        QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, QPoint(110, 80));
        QTRY_COMPARE(images.size(), 1);
        QCOMPARE(images[0][0].value<QImage>(), original);
        QCOMPARE(grabs, 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        input.captureRegion();
        selector = owner.findChild<RegionSelector *>();
        QVERIFY(selector);
        QTest::mousePress(selector, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTest::mouseRelease(selector, Qt::LeftButton, Qt::NoModifier, QPoint(110, 80));
        input.stop();
        QTest::qWait(100);
        QCOMPARE(grabs, 1);
        QCOMPARE(images.size(), 1);
    }
    void selectedTextRequiresFreshClipboardAndOneCopy() {
        int copies = 0, reads = 0;
        quint64 revision = 8;
        SelectionBackend backend;
        backend.foreground = [] { return quintptr(123); };
        backend.external = [](quintptr id) { return id == 123; };
        backend.released = [] { return true; };
        backend.clipboardRevision = [&] { return revision; };
        backend.clipboardMatchesTarget = [](quintptr) { return true; };
        backend.copy = [&] {
            ++copies;
            revision = 9;
            return true;
        };
        backend.readText = [&](quint64 version, SelectionTextCallback done) {
            ++reads;
            if (version == 9)
                done("What is a mutex?", {});
            else
                done({}, "wrong clipboard revision");
        };
        SelectionCopy copy(nullptr, backend);
        QSignalSpy result(&copy, &SelectionCopy::finished);
        copy.start();
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(result[0][0].toString(), QString("What is a mutex?"));
        QVERIFY(result[0][1].toString().isEmpty());
        QCOMPARE(copies, 1);
        QCOMPARE(reads, 1);
    }
    void cancellingBeforeModifierReleaseDoesNotCopyOrSubmit() {
        int copies = 0;
        SelectionBackend backend;
        backend.foreground = [] { return quintptr(123); };
        backend.external = [](quintptr) { return true; };
        backend.released = [] { return false; };
        backend.clipboardRevision = [] { return quint64(5); };
        backend.clipboardMatchesTarget = [](quintptr) { return true; };
        backend.copy = [&] {
            ++copies;
            return true;
        };
        backend.readText = [](quint64, SelectionTextCallback done) { done("stale", {}); };
        SelectionCopy copy(nullptr, backend);
        QSignalSpy result(&copy, &SelectionCopy::finished);
        copy.start();
        copy.cancel();
        QTest::qWait(80);
        QCOMPARE(copies, 0);
        QCOMPARE(result.size(), 0);
    }
    void unrelatedClipboardUpdateIsNotSubmitted() {
        quint64 revision = 20;
        int reads = 0;
        SelectionBackend backend;
        backend.foreground = [] { return quintptr(123); };
        backend.external = [](quintptr) { return true; };
        backend.released = [] { return true; };
        backend.clipboardRevision = [&] { return revision; };
        backend.clipboardMatchesTarget = [](quintptr) { return false; };
        backend.copy = [&] {
            ++revision;
            return true;
        };
        backend.readText = [&](quint64, SelectionTextCallback done) {
            ++reads;
            done("UNRELATED CLIPBOARD", {});
        };
        SelectionCopy copy(nullptr, backend);
        QSignalSpy done(&copy, &SelectionCopy::finished);
        copy.start();
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done[0][0].toString().isEmpty());
        QVERIFY(!done[0][1].toString().isEmpty());
        QCOMPARE(reads, 0);
    }
    void selectorNeverShowsWhenCaptureExclusionFails() {
        QWidget owner;
        int attempts = 0;
        RegionSelector selector(QRect(0, 0, 640, 480), &owner, [&](quintptr, bool on) {
            ++attempts;
            return WindowCaptureResult{!on, QStringLiteral("fixture capture exclusion failure")};
        });
        QString error;
        QVERIFY(!selector.start(&error));
        QVERIFY(!selector.isVisible());
        QCOMPARE(attempts, 1);
        QVERIFY(error.contains("fixture capture exclusion failure"));
    }
    void screenshotSelectorStaysProtectedWhenMainPrivacyTurnsOff() {
        QWidget owner;
        QHash<quintptr, bool> flags;
        const auto backend = [&](quintptr id, bool on) {
            flags[id] = on;
            return WindowCaptureResult{true, {}};
        };
        CapturePrivacy privacy(&owner, backend);
        owner.show();
        RegionSelector selector(QRect(0, 0, 640, 480), &owner, backend);
        QVERIFY(selector.start());
        const auto id = quintptr(selector.winId());
        privacy.setRequested(true);
        privacy.setRequested(false);
        QVERIFY(flags.value(id));
    }
    void selectorConvertsLocalDragToScreenCoordinates() {
        QWidget owner;
        RegionSelector selector(QRect(-800, 80, 640, 480), &owner,
                                [](quintptr, bool) { return WindowCaptureResult{true, {}}; });
        QSignalSpy selected(&selector, &RegionSelector::selected);
        QVERIFY(selector.start());
        QTest::mousePress(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(30, 40));
        QTest::mouseMove(&selector, QPoint(130, 160));
        QTest::mouseRelease(&selector, Qt::LeftButton, Qt::NoModifier, QPoint(130, 160));
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected[0][0].toRect(), QRect(-770, 120, 101, 121));
        QVERIFY(!selector.isVisible());
    }
};
QTEST_MAIN(WrittenCaptureTest)
#include "tst_written_capture.moc"
