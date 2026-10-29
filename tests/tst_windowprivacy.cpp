#include "app/WindowCapture.h"
#include "ui/CapturePrivacy.h"
#include <QDialog>
#include <QScopeGuard>
#include <QtTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class RecreatedWindow : public QWidget {
  public:
    void recreate() {
        hide();
        destroy();
        show();
    }
};
class WindowPrivacyTest : public QObject {
    Q_OBJECT
  private slots:
    void defaultOffThenAppliesAndClearsWithoutHidingLocalWindow() {
        QWidget root;
        root.show();
        QHash<quintptr, bool> flags;
        CapturePrivacy privacy(&root, [&](quintptr id, bool on) {
            flags[id] = on;
            return WindowCaptureResult{true, {}};
        });
        QVERIFY(!privacy.requested());
        QVERIFY(privacy.fullyApplied());
        QVERIFY(flags.isEmpty());
        privacy.setRequested(true);
        QVERIFY(privacy.requested());
        QVERIFY(privacy.fullyApplied());
        QVERIFY(flags.value(root.winId()));
        QVERIFY(root.isVisible());
        privacy.setRequested(false);
        QVERIFY(!privacy.requested());
        QVERIFY(privacy.fullyApplied());
        QVERIFY(!flags.value(root.winId()));
        QVERIFY(root.isVisible());
    }
    void newDialogsAndApplicationPopupsAreCoveredButOtherWindowsAreIgnored() {
        QWidget root;
        root.show();
        QHash<quintptr, bool> flags;
        CapturePrivacy privacy(&root, [&](quintptr id, bool on) {
            flags[id] = on;
            return WindowCaptureResult{true, {}};
        });
        privacy.setRequested(true);
        QDialog dialog(&root);
        dialog.show();
        QWidget popup(nullptr, Qt::Popup);
        popup.show();
        QWidget other;
        other.show();
        QCoreApplication::processEvents();
        QVERIFY(flags.value(dialog.winId()));
        QVERIFY(flags.value(popup.winId()));
        QVERIFY(!flags.contains(other.winId()));
        privacy.setRequested(false);
        QVERIFY(!flags.value(root.winId()));
        QVERIFY(!flags.value(dialog.winId()));
        QVERIFY(!flags.value(popup.winId()));
        QVERIFY(other.isVisible());
    }
    void requestCanBeAppliedBeforeFirstShowAndReappliedAfterHandleRecreation() {
        RecreatedWindow root;
        QList<quintptr> applied;
        CapturePrivacy privacy(&root, [&](quintptr id, bool on) {
            if (on)
                applied.append(id);
            return WindowCaptureResult{true, {}};
        });
        privacy.setRequested(true);
        QVERIFY(!root.isVisible());
        QVERIFY(applied.contains(root.winId()));
        QVERIFY(privacy.fullyApplied());
        root.show();
        applied.clear();
        root.recreate();
        QCoreApplication::processEvents();
        QVERIFY(applied.contains(root.winId()));
        QVERIFY(privacy.fullyApplied());
        applied.clear();
        QEvent changed(QEvent::WinIdChange);
        QCoreApplication::sendEvent(&root, &changed);
        QVERIFY(applied.contains(root.winId()));
    }
    void failedDialogIsNotReportedAsCompleteAndCanRecover() {
        QWidget root;
        root.show();
        quintptr rejected = 0;
        bool fail = true;
        CapturePrivacy privacy(&root, [&](quintptr id, bool on) {
            return on && id == rejected && fail ? WindowCaptureResult{false, "fixture denied"}
                                                : WindowCaptureResult{true, {}};
        });
        privacy.setRequested(true);
        auto dialog = new QDialog(&root);
        rejected = dialog->winId();
        dialog->show();
        QCoreApplication::processEvents();
        QVERIFY(!privacy.fullyApplied());
        QVERIFY(privacy.error().contains("fixture denied"));
        QVERIFY(dialog->isVisible());
        fail = false;
        privacy.setRequested(true);
        QVERIFY(privacy.fullyApplied());
        QVERIFY(privacy.error().isEmpty());
        fail = true;
        privacy.setRequested(true);
        QVERIFY(!privacy.fullyApplied());
        delete dialog;
        QVERIFY(privacy.fullyApplied());
    }
    void clearFailureRemainsVisibleUntilRetried() {
        QWidget root;
        root.show();
        bool failClear = true;
        CapturePrivacy privacy(&root, [&](quintptr, bool on) {
            return !on && failClear ? WindowCaptureResult{false, "fixture clear failed"}
                                    : WindowCaptureResult{true, {}};
        });
        privacy.setRequested(true);
        privacy.setRequested(false);
        QVERIFY(!privacy.requested());
        QVERIFY(!privacy.fullyApplied());
        QVERIFY(privacy.error().contains("fixture clear failed"));
        failClear = false;
        privacy.setRequested(false);
        QVERIFY(privacy.fullyApplied());
        QVERIFY(privacy.error().isEmpty());
        QVERIFY(root.isVisible());
    }
    void reentrantHandleNotificationDoesNotRecurse() {
        QWidget root;
        root.show();
        int calls = 0;
        CapturePrivacy privacy(&root, [&](quintptr, bool) {
            ++calls;
            if (calls < 4) {
                QEvent changed(QEvent::WinIdChange);
                QCoreApplication::sendEvent(&root, &changed);
            }
            return WindowCaptureResult{true, {}};
        });
        privacy.setRequested(true);
        QCoreApplication::processEvents();
        QVERIFY(calls > 0);
        QVERIFY(calls < 3);
        QVERIFY(privacy.fullyApplied());
    }
    void versionFloorRejectsBlackoutFallback_data() {
        QTest::addColumn<int>("major");
        QTest::addColumn<int>("build");
        QTest::addColumn<bool>("supported");
        QTest::newRow("windows8") << 6 << 9600 << false;
        QTest::newRow("before2004") << 10 << 19040 << false;
        QTest::newRow("2004") << 10 << 19041 << true;
        QTest::newRow("windows11") << 10 << 26100 << true;
    }
    void versionFloorRejectsBlackoutFallback() {
        QFETCH(int, major);
        QFETCH(int, build);
        QFETCH(bool, supported);
        QCOMPARE(WindowCapture::supportedVersion(major, build), supported);
    }
    void nativeOwnedHiddenWindowRoundTrips_data() {
        QTest::addColumn<bool>("layered");
        QTest::newRow("ordinary") << false;
        QTest::newRow("layered") << true;
    }
    void nativeOwnedHiddenWindowRoundTrips() {
#ifdef Q_OS_WIN
        QFETCH(bool, layered);
        const auto unsupported = WindowCapture::supportError();
        if (!unsupported.isEmpty())
            QSKIP(qPrintable(unsupported));
        auto handle = CreateWindowExW(layered ? WS_EX_LAYERED : 0, L"STATIC", L"capture-test", WS_POPUP, 0, 0,
                                      16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        QVERIFY(handle);
        auto cleanup = qScopeGuard([&] { DestroyWindow(handle); });
        QVERIFY(!IsWindowVisible(handle));
        const auto enabled = WindowCapture::setExcluded(quintptr(handle), true);
        QVERIFY2(enabled.ok, qPrintable(enabled.error));
        DWORD actual = 0;
        QVERIFY(GetWindowDisplayAffinity(handle, &actual));
        QCOMPARE(actual, DWORD(0x11));
        const auto disabled = WindowCapture::setExcluded(quintptr(handle), false);
        QVERIFY2(disabled.ok, qPrintable(disabled.error));
        QVERIFY(GetWindowDisplayAffinity(handle, &actual));
        QCOMPARE(actual, DWORD(0));
        QVERIFY(!IsWindowVisible(handle));
#else
        QSKIP("Windows native API test");
#endif
    }
    void nativeInvalidAndChildHandlesAreRejected() {
        const auto invalid = WindowCapture::setExcluded(0, true);
        QVERIFY(!invalid.ok);
        QVERIFY(!invalid.error.isEmpty());
#ifdef Q_OS_WIN
        auto root = CreateWindowExW(0, L"STATIC", L"capture-root", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr,
                                    GetModuleHandleW(nullptr), nullptr);
        QVERIFY(root);
        auto cleanup = qScopeGuard([&] { DestroyWindow(root); });
        auto child = CreateWindowExW(0, L"STATIC", L"capture-child", WS_CHILD, 0, 0, 8, 8, root, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        QVERIFY(child);
        const auto result = WindowCapture::setExcluded(quintptr(child), true);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
#endif
    }
};
QTEST_MAIN(WindowPrivacyTest)
#include "tst_windowprivacy.moc"
