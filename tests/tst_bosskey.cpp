#include "app/GlobalHotkey.h"
#include "app/NetworkRuntime.h"
#include "ui/MainWindow.h"
#include "ui/BossVisibility.h"
#include "ui/SettingsDialog.h"
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QStandardPaths>
#include <QTimer>
#include <QScopeGuard>
#include <QtTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class BossKeyIdleSource : public AudioSource {
    void run() override {
        QAudioFormat format;
        format.setSampleRate(24000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        emit formatReady(format);
        exec();
    }
};

class BossKeyTest : public QObject {
    Q_OBJECT
    QTemporaryDir profile{QDir::currentPath() + "/boss-key-XXXXXX"};
    static HotkeyBackend success() {
        return [](int, quint32, quint32, bool) { return HotkeyResult{true, {}}; };
    }
  private slots:
    void initTestCase() {
        QVERIFY(profile.isValid());
        QCoreApplication::setOrganizationName("InterviewBossKeyTests");
        QCoreApplication::setApplicationName("BossKey");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
        QStandardPaths::setTestModeEnabled(true);
        NetworkRuntime::initializeTls();
    }
    void init() {
        QSettings().clear();
        for (const auto &key : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
            QSettings().setValue(key, "https://boss-key-fixture.invalid");
    }
    void hideAndRestoreKeepsPrivacyAndSession() {
        const auto capture = [](quintptr, bool) { return WindowCaptureResult{true, {}}; };
        MainWindow window(nullptr, profile.path() + "/data-" + newId(), capture, capture, success());
        window.show();
        QTest::qWait(40);
        auto privacy = window.findChild<QPushButton *>("capturePrivacyToggle");
        auto hide = window.findChild<QPushButton *>("hideWindowButton");
        auto key = window.findChild<GlobalHotkey *>();
        auto visibility = window.findChild<BossVisibility *>();
        auto controller = window.findChild<SessionController *>();
        QVERIFY(privacy && hide && key && visibility && controller);
        controller->autoExtract = false;
        controller->autoAnswer = false;
        controller->audio.renderFactory = [](const QString &) -> AudioSource * {
            return new BossKeyIdleSource;
        };
        controller->audio.microphoneFactory = [](const QByteArray &) -> AudioSource * {
            return new BossKeyIdleSource;
        };
        const auto cleanup = qScopeGuard([&] {
            if (controller->recording())
                controller->discardRecording();
        });
        window.findChild<QPushButton *>("startButton")->click();
        QVERIFY(controller->recording());
        privacy->click();
        const auto handle = window.winId();
        const auto session = controller->liveSessionId();
        QVERIFY(hide->isEnabled());
        hide->click();
        QVERIFY(visibility->hidden());
        QVERIFY(!window.isVisible());
        QVERIFY(controller->recording());
        QVERIFY(!controller->paused());
        key->activated();
        QVERIFY(!visibility->hidden());
        QVERIFY(window.isVisible());
        QCOMPARE(window.winId(), handle);
        QVERIFY(privacy->isChecked());
        QCOMPARE(controller->liveSessionId(), session);
        QVERIFY(controller->recording());
        QVERIFY(!controller->paused());
    }
    void conflictingChangeKeepsWorkingKeyAndSavedSetting() {
        bool occupied = false;
        int activeId = 0;
        GlobalHotkey key(nullptr, [&](int id, quint32, quint32, bool enable) {
            if (enable && occupied)
                return HotkeyResult{false, "fixture key occupied"};
            activeId = enable ? id : 0;
            return HotkeyResult{true, {}};
        });
        QVERIFY(key.setSequence(QKeySequence("Ctrl+Alt+Space")).ok);
        const int originalId = activeId;
        QSettings().setValue("shortcuts/bossKey", "Ctrl+Alt+Space");
        SettingsDialog dialog(nullptr, &key);
        auto editor = dialog.findChild<QKeySequenceEdit *>("bossKeySequence");
        auto apply = dialog.findChild<QPushButton *>("applyBossKey");
        auto status = dialog.findChild<QLabel *>("bossKeyStatus");
        QVERIFY(editor && apply && status);
        occupied = true;
        editor->setKeySequence(QKeySequence("Ctrl+Alt+F8"));
        apply->click();
        QCOMPARE(activeId, originalId);
        QCOMPARE(key.sequence(), QKeySequence("Ctrl+Alt+Space"));
        QCOMPARE(QSettings().value("shortcuts/bossKey").toString(), QString("Ctrl+Alt+Space"));
        QVERIFY(status->text().contains("fixture key occupied"));
    }
    void savedKeyIsUsedAfterRestart() {
        {
            GlobalHotkey key(nullptr, success());
            QVERIFY(key.setSequence(GlobalHotkey::defaultSequence()).ok);
            SettingsDialog settings(nullptr, &key);
            settings.findChild<QKeySequenceEdit *>("bossKeySequence")->setKeySequence(QKeySequence("F8"));
            settings.findChild<QPushButton *>("applyBossKey")->click();
            QCOMPARE(key.sequence(), QKeySequence("F8"));
        }
        quint32 modifiers = 99, vk = 0;
        MainWindow window(nullptr, profile.path() + "/restart-" + newId(), {}, {},
                          [&](int, quint32 mods, quint32 key, bool) {
                              modifiers = mods;
                              vk = key;
                              return HotkeyResult{true, {}};
                          });
        QCOMPARE(modifiers, quint32(0));
        QCOMPARE(vk, quint32(119));
        QVERIFY(window.findChild<QPushButton *>("hideWindowButton")->isEnabled());
    }
    void unavailableUnchangedKeyDoesNotBlockModelSettings() {
        GlobalHotkey key(
            nullptr, [](int, quint32, quint32, bool) { return HotkeyResult{false, "fixture unavailable"}; });
        QVERIFY(!key.setSequence(GlobalHotkey::defaultSequence()).ok);
        SettingsDialog settings(nullptr, &key);
        auto effort = settings.findChild<QComboBox *>("chatgptReasoningEffort");
        QVERIFY(effort);
        effort->setCurrentIndex(effort->findData("medium"));
        auto buttons = settings.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        buttons->button(QDialogButtonBox::Save)->click();
        QCOMPARE(settings.result(), int(QDialog::Accepted));
        QCOMPARE(QSettings().value("ChatGPT/reasoningEffort").toString(), QString("medium"));
        QVERIFY(!key.registered());
    }
    void rollbackFailureDoesNotForgetRegisteredCandidate() {
        QSet<int> nativeKeys;
        int originalId = 0;
        bool rejectRemoval = true;
        {
            GlobalHotkey key(nullptr, [&](int id, quint32, quint32, bool on) {
                if (!on && rejectRemoval)
                    return HotkeyResult{false, id == originalId ? "old release failed" : "rollback failed"};
                if (on)
                    nativeKeys.insert(id);
                else
                    nativeKeys.remove(id);
                return HotkeyResult{true, {}};
            });
            QVERIFY(key.setSequence(QKeySequence("F8")).ok);
            originalId = nativeKeys.values().first();
            const auto result = key.setSequence(QKeySequence("F9"));
            QVERIFY(!result.ok);
            QCOMPARE(nativeKeys.size(), 2);
            QCOMPARE(key.sequence(), QKeySequence("F8"));
            rejectRemoval = false;
        }
        QVERIFY(nativeKeys.isEmpty());
    }
    void failedOldKeyRemovalRollsBackNewRegistration() {
        QHash<int, quint32> registered;
        int originalId = 0;
        GlobalHotkey key(nullptr, [&](int id, quint32, quint32 vk, bool on) {
            if (!on && id == originalId)
                return HotkeyResult{false, "fixture unregister failure"};
            if (on)
                registered[id] = vk;
            else
                registered.remove(id);
            return HotkeyResult{true, {}};
        });
        QVERIFY(key.setSequence(QKeySequence("F8")).ok);
        originalId = registered.keys().first();
        QVERIFY(!key.setSequence(QKeySequence("F9")).ok);
        QCOMPARE(key.sequence(), QKeySequence("F8"));
        QCOMPARE(registered.size(), 1);
        QCOMPARE(registered.value(originalId), quint32(119));
        originalId = 0; // Allow ordinary destructor cleanup.
    }
    void invalidKeysAreRejectedWithoutRegistration_data() {
        QTest::addColumn<QString>("key");
        for (const auto &key : {"", "A", "Shift+A", "F12", "Alt+F4", "Meta+A", "Ctrl+Shift+O", "Ctrl+Shift+P",
                                "Ctrl+A, Ctrl+B"})
            QTest::newRow(key) << QString(key);
    }
    void invalidKeysAreRejectedWithoutRegistration() {
        QFETCH(QString, key);
        int calls = 0;
        GlobalHotkey hotkey(nullptr, [&](int, quint32, quint32, bool) {
            ++calls;
            return HotkeyResult{true, {}};
        });
        QVERIFY(!hotkey.setSequence(QKeySequence(key)).ok);
        QCOMPARE(calls, 0);
        QVERIFY(!hotkey.registered());
    }
    void rebindIsTransactionalAndCleanupReleasesKey() {
        QList<QString> calls;
        {
            GlobalHotkey key(nullptr, [&](int id, quint32 mods, quint32 vk, bool on) {
                calls << QString("%1:%2:%3:%4").arg(on).arg(id).arg(mods).arg(vk);
                return HotkeyResult{true, {}};
            });
            QVERIFY(key.setSequence(QKeySequence("Ctrl+Alt+Space")).ok);
            QVERIFY(key.setSequence(QKeySequence("Ctrl+Alt+Space")).ok);
            QCOMPARE(calls.size(), 1);
            const auto id = calls[0].section(':', 1, 1).toInt();
            QVERIFY(key.setSequence(QKeySequence("F8")).ok);
            QCOMPARE(calls.size(), 3);
            QCOMPARE(calls[0], QString("1:%1:3:32").arg(id));
            QCOMPARE(calls[1], QString("1:%1:0:119").arg(id + 1));
            QCOMPARE(calls[2], QString("0:%1:3:32").arg(id));
#ifdef Q_OS_WIN
            QSignalSpy fired(&key, &GlobalHotkey::activated);
            MSG message{};
            message.message = WM_HOTKEY;
            message.wParam = id;
            message.lParam = MAKELPARAM(3, 32);
            qintptr result = 0;
            QVERIFY(!key.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            message.wParam = id + 1;
            message.lParam = MAKELPARAM(0, 119);
            QVERIFY(key.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            QCOMPARE(fired.size(), 1);
            message.hwnd = reinterpret_cast<HWND>(quintptr(1));
            QVERIFY(!key.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            message.hwnd = nullptr;
            message.lParam = MAKELPARAM(2, 119);
            QVERIFY(!key.nativeEventFilter("windows_dispatcher_MSG", &message, &result));
            QCOMPARE(fired.size(), 1);
#endif
        }
        QCOMPARE(calls.size(), 4);
        QVERIFY(calls.last().startsWith("0:"));
        QVERIFY(calls.last().endsWith(":0:119"));
    }
#ifdef Q_OS_WIN
    void queuedWindowsMessageTogglesHiddenWindow() {
        int id = 0;
        MainWindow window(nullptr, profile.path() + "/message-" + newId(), {}, {},
                          [&](int registeredId, quint32, quint32, bool on) {
                              if (on)
                                  id = registeredId;
                              return HotkeyResult{true, {}};
                          });
        window.show();
        QTest::qWait(20);
        auto visibility = window.findChild<BossVisibility *>();
        QVERIFY(visibility);
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, id, MAKELPARAM(3, 32)));
        QTRY_VERIFY(visibility->hidden());
        QVERIFY(PostThreadMessageW(GetCurrentThreadId(), WM_HOTKEY, id, MAKELPARAM(3, 32)));
        QTRY_VERIFY(!visibility->hidden());
        QVERIFY(window.isVisible());
    }
#endif
};
QTEST_MAIN(BossKeyTest)
#include "tst_bosskey.moc"
