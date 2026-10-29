#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/PreparationDialog.h"
#include <QAbstractScrollArea>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QSlider>
#include <QScrollArea>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QHeaderView>
#include <QTableWidget>
#include <QTabWidget>
#include <QtTest>

class AppearanceIdleSource : public AudioSource {
    void run() override {
        QAudioFormat format;
        format.setSampleRate(24000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        emit formatReady(format);
        exec();
    }
};

class AppearanceTest : public QObject {
    Q_OBJECT
    QTemporaryDir profile{QDir::currentPath() + "/appearance-XXXXXX"};

    QString dataPath() const {
        return profile.path() + "/data-" + newId();
    }
    static void sendMouse(QWidget *target, QEvent::Type type, QPoint global, Qt::MouseButton button,
                          Qt::MouseButtons buttons) {
        QMouseEvent event(type, target->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &event);
    }
    static int surfaceAlpha(MainWindow &window) {
        return window.grab().toImage().pixelColor(3, window.height() / 2).alpha();
    }
    static void screenshot(QWidget &window, const QString &name) {
        const auto directory = qEnvironmentVariable("INTERVIEW_APPEARANCE_SCREENSHOT");
        if (!directory.isEmpty())
            QVERIFY(window.grab().save(directory + "/" + name + ".png"));
    }
  private slots:
    void initTestCase() {
        QVERIFY(profile.isValid());
        QCoreApplication::setOrganizationName("InterviewAppearanceTests");
        QCoreApplication::setApplicationName("Appearance");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
        QStandardPaths::setTestModeEnabled(true);
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    }
    void init() {
        QSettings().clear();
        for (const auto &key : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
            QSettings().setValue(key, "https://" + newId() + ".invalid");
    }
    void privacyStartsOffEveryLaunch() {
        QSettings().setValue("privacy/excludeCapture", true);
        int calls = 0;
        const WindowCaptureBackend backend = [&](quintptr, bool) {
            ++calls;
            return WindowCaptureResult{true, {}};
        };
        {
            MainWindow window(nullptr, dataPath(), backend);
            window.show();
            QTest::qWait(30);
            auto toggle = window.findChild<QPushButton *>("capturePrivacyToggle");
            auto status = window.findChild<QLabel *>("capturePrivacyStatus");
            QVERIFY(toggle);
            QVERIFY(status);
            QVERIFY(!toggle->isChecked());
            QVERIFY(status->text().contains(QStringLiteral("关闭")));
            QCOMPARE(calls, 0);
            toggle->click();
            QVERIFY(toggle->isChecked());
            QVERIFY(calls > 0);
            QVERIFY(status->text().contains(QStringLiteral("已设置")));
            QVERIFY(status->text().contains(QStringLiteral("需实测")));
            QVERIFY(window.isVisible());
        }
        calls = 0;
        MainWindow reopened(nullptr, dataPath(), backend);
        auto toggle = reopened.findChild<QPushButton *>("capturePrivacyToggle");
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        QCOMPARE(calls, 0);
    }
    void privacyAndAppearancePreserveRecordingAndCoverSettings() {
        QHash<quintptr, bool> flags;
        MainWindow window(nullptr, dataPath(), [&](quintptr id, bool on) {
            flags[id] = on;
            return WindowCaptureResult{true, {}};
        });
        auto privacy = window.findChild<QPushButton *>("capturePrivacyToggle");
        auto appearance = window.findChild<QCheckBox *>("transparentBackground");
        auto status = window.findChild<QLabel *>("capturePrivacyStatus");
        QVERIFY(privacy);
        QVERIFY(appearance);
        QVERIFY(status);
        auto controller = window.findChild<SessionController *>();
        QVERIFY(controller);
        controller->autoExtract = false;
        controller->autoAnswer = false;
        controller->audio.renderFactory = [](const QString &) -> AudioSource * {
            return new AppearanceIdleSource;
        };
        controller->audio.microphoneFactory = [](const QByteArray &) -> AudioSource * {
            return new AppearanceIdleSource;
        };
        auto stop = qScopeGuard([&] {
            if (controller->recording())
                controller->discardRecording();
        });
        window.show();
        QTest::qWait(30);
        auto start = window.findChild<QPushButton *>("startButton");
        QVERIFY(start);
        start->click();
        QVERIFY(controller->recording());
        const auto session = controller->liveSessionId();
        const auto id = quintptr(window.winId());
        privacy->click();
        QVERIFY(flags.value(id));
        appearance->setChecked(true);
        appearance->setChecked(false);
        appearance->setChecked(true);
        QCOMPARE(quintptr(window.winId()), id);
        QVERIFY(flags.value(id));
        QVERIFY(controller->recording());
        QCOMPARE(controller->liveSessionId(), session);
        SettingsDialog dialog(&window);
        dialog.show();
        QTest::qWait(30);
        const auto dialogId = quintptr(dialog.winId());
        QVERIFY(flags.value(dialogId));
        QVERIFY(status->text().contains(QStringLiteral("已设置")));
        privacy->click();
        QVERIFY(!flags.value(id));
        QVERIFY(!flags.value(dialogId));
        QVERIFY(window.isVisible());
        QVERIFY(dialog.isVisible());
        QVERIFY(controller->recording());
        QVERIFY(controller->discardRecording());
    }
    void privacyErrorsAndFailedClearAreVisible() {
        bool failEnable = true;
        bool failClear = false;
        MainWindow window(nullptr, dataPath(), [&](quintptr, bool on) {
            return (on ? failEnable : failClear) ? WindowCaptureResult{false, "fixture capture failure"}
                                                 : WindowCaptureResult{true, {}};
        });
        window.show();
        auto toggle = window.findChild<QPushButton *>("capturePrivacyToggle");
        auto status = window.findChild<QLabel *>("capturePrivacyStatus");
        QVERIFY(toggle);
        QVERIFY(status);
        toggle->click();
        QVERIFY(toggle->isChecked());
        QVERIFY(status->property("privacyError").toBool());
        QVERIFY(status->text().contains(QStringLiteral("未完全生效")));
        QVERIFY(status->text().contains("fixture capture failure"));
        QVERIFY(!status->text().contains(QStringLiteral("已设置")));
        toggle->click();
        QVERIFY(!status->property("privacyError").toBool());
        failEnable = false;
        toggle->click();
        QVERIFY(status->text().contains(QStringLiteral("已设置")));
        failClear = true;
        toggle->click();
        QVERIFY(!toggle->isChecked());
        QVERIFY(status->property("privacyError").toBool());
        QVERIFY(status->text().contains(QStringLiteral("关闭失败")));
        QVERIFY(window.isVisible());
    }
    void failedOwnedDialogUpdatesMainStatus() {
        quintptr rootId = 0;
        MainWindow window(nullptr, dataPath(), [&](quintptr id, bool on) {
            return on && id != rootId ? WindowCaptureResult{false, "fixture dialog failure"}
                                      : WindowCaptureResult{true, {}};
        });
        window.show();
        rootId = quintptr(window.winId());
        auto toggle = window.findChild<QPushButton *>("capturePrivacyToggle");
        auto status = window.findChild<QLabel *>("capturePrivacyStatus");
        QVERIFY(toggle);
        QVERIFY(status);
        toggle->click();
        QVERIFY(status->text().contains(QStringLiteral("已设置")));
        auto dialog = new QDialog(&window);
        dialog->show();
        QVERIFY(status->text().contains(QStringLiteral("未完全生效")));
        QVERIFY(status->text().contains("fixture dialog failure"));
        delete dialog;
        QVERIFY(status->text().contains(QStringLiteral("已设置")));
    }
    void backgroundAlphaChangesWithoutFadingTextOrRecreatingWindow() {
        MainWindow window(nullptr, dataPath());
        auto toggle = window.findChild<QCheckBox *>("transparentBackground");
        auto opacity = window.findChild<QSlider *>("backgroundOpacity");
        QVERIFY(toggle);
        QVERIFY(opacity);
        QVERIFY(!toggle->isChecked());
        QCOMPARE(opacity->value(), 80);
        QVERIFY(!opacity->isEnabled());
        window.show();
        QTest::qWait(60);
        window.resize(1280, 960);
        const auto handle = window.winId();
        QCOMPARE(surfaceAlpha(window), 255);
        screenshot(window, "appearance-normal");
        toggle->setChecked(true);
        QVERIFY(opacity->isEnabled());
        for (int value : {40, 80, 95}) {
            opacity->setValue(value);
            QCoreApplication::processEvents();
            QVERIFY(qAbs(surfaceAlpha(window) - qRound(value * 255.0 / 100)) <= 2);
            QCOMPARE(window.windowOpacity(), qreal(1));
            QCOMPARE(window.winId(), handle);
            auto caption = window.findChild<QLabel *>("caption");
            QVERIFY(caption);
            QCOMPARE(caption->palette().color(QPalette::WindowText).alpha(), 255);
            QVERIFY(caption->palette().color(QPalette::WindowText).lightnessF() > 0.8);
            const auto glyphs = caption->grab().toImage();
            int opaquePixels = 0;
            for (int y = 0; y < glyphs.height(); ++y)
                for (int x = 0; x < glyphs.width(); ++x) {
                    const auto pixel = glyphs.pixelColor(x, y);
                    if (pixel.alpha() >= 250 && pixel.lightnessF() > 0.8)
                        ++opaquePixels;
                }
            QVERIFY(opaquePixels > 10);
        }
        opacity->setValue(80);
        screenshot(window, "appearance-glass");
        toggle->setChecked(false);
        QCOMPARE(surfaceAlpha(window), 255);
        QCOMPARE(window.winId(), handle);
        QVERIFY(window.findChild<QLabel *>("caption")->palette().color(QPalette::WindowText).lightnessF() <
                0.4);
    }
    void opacitySettingsAreValidated_data() {
        QTest::addColumn<QVariant>("saved");
        QTest::addColumn<int>("expected");
        QTest::newRow("invalid") << QVariant("not-a-number") << 80;
        QTest::newRow("too-low") << QVariant(-5) << 40;
        QTest::newRow("too-high") << QVariant(150) << 95;
        QTest::newRow("valid") << QVariant(67) << 67;
    }
    void opacitySettingsAreValidated() {
        QFETCH(QVariant, saved);
        QFETCH(int, expected);
        QSettings().setValue("appearance/backgroundOpacity", saved);
        MainWindow window(nullptr, dataPath());
        auto opacity = window.findChild<QSlider *>("backgroundOpacity");
        QVERIFY(opacity);
        QCOMPARE(opacity->minimum(), 40);
        QCOMPARE(opacity->maximum(), 95);
        QCOMPARE(opacity->value(), expected);
    }
    void appearanceCanBeRestoredAfterRestart() {
        {
            MainWindow window(nullptr, dataPath());
            auto toggle = window.findChild<QCheckBox *>("transparentBackground");
            auto opacity = window.findChild<QSlider *>("backgroundOpacity");
            QVERIFY(toggle);
            QVERIFY(opacity);
            toggle->setChecked(true);
            opacity->setValue(67);
        }
        MainWindow reopened(nullptr, dataPath());
        auto toggle = reopened.findChild<QCheckBox *>("transparentBackground");
        auto opacity = reopened.findChild<QSlider *>("backgroundOpacity");
        QVERIFY(toggle);
        QVERIFY(opacity);
        QVERIFY(toggle->isChecked());
        QCOMPARE(opacity->value(), 67);
        reopened.show();
        QTest::qWait(60);
        QVERIFY(qAbs(surfaceAlpha(reopened) - 171) <= 2);
        toggle->setChecked(false);
        QCOMPARE(surfaceAlpha(reopened), 255);
    }
    void windowControlsAndResizeRemainAvailable() {
        MainWindow window(nullptr, dataPath());
        auto maximize = window.findChild<QPushButton *>("windowMaximize");
        auto minimize = window.findChild<QPushButton *>("windowMinimize");
        auto close = window.findChild<QPushButton *>("windowClose");
        auto bar = window.findChild<QWidget *>("windowTitleBar");
        auto right = window.findChild<QWidget *>("resizeRight");
        QVERIFY(maximize);
        QVERIFY(minimize);
        QVERIFY(close);
        QVERIFY(bar);
        QVERIFY(right);
        window.show();
        QTest::qWait(60);
        const auto origin = window.pos();
        const auto press = bar->mapToGlobal(QPoint(80, 15));
        sendMouse(bar, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
        sendMouse(bar, QEvent::MouseMove, press + QPoint(20, 15), Qt::NoButton, Qt::LeftButton);
        sendMouse(bar, QEvent::MouseButtonRelease, press + QPoint(20, 15), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(window.pos(), origin + QPoint(20, 15));
        const auto oldWidth = window.width();
        const auto edge = right->mapToGlobal(right->rect().center());
        sendMouse(right, QEvent::MouseButtonPress, edge, Qt::LeftButton, Qt::LeftButton);
        sendMouse(right, QEvent::MouseMove, edge - QPoint(30, 0), Qt::NoButton, Qt::LeftButton);
        sendMouse(right, QEvent::MouseButtonRelease, edge - QPoint(30, 0), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(window.width(), oldWidth - 30);
        maximize->click();
        QTRY_VERIFY(window.isMaximized());
        QVERIFY(!right->isVisible());
        maximize->click();
        QTRY_VERIFY(!window.isMaximized());
        QVERIFY(right->isVisible());
        minimize->click();
        QVERIFY(window.isMinimized());
        window.showNormal();
        close->click();
        QVERIFY(!window.isVisible());
    }
    void settingsStayOpaqueAndReadableInGlassMode() {
        MainWindow window(nullptr, dataPath());
        auto toggle = window.findChild<QCheckBox *>("transparentBackground");
        QVERIFY(toggle);
        toggle->setChecked(true);
        SettingsDialog dialog(&window);
        dialog.show();
        QTest::qWait(30);
        const auto image = dialog.grab().toImage();
        QCOMPARE(image.pixelColor(2, 2).alpha(), 255);
        QVERIFY(image.pixelColor(2, 2).lightnessF() < 0.4);
        QVERIFY(dialog.palette().color(QPalette::WindowText).lightnessF() > 0.8);
        int visibleAreas = 0;
        for (auto area : dialog.findChildren<QScrollArea *>()) {
            if (!area->isVisible())
                continue;
            ++visibleAreas;
            const auto viewport = area->viewport()->grab().toImage();
            QVERIFY2(viewport.pixelColor(2, 2).lightnessF() < 0.4,
                     "A settings scroll viewport painted light behind light text");
        }
        QVERIFY(visibleAreas > 0);
        screenshot(dialog, "appearance-settings-glass");
    }
    void preparationTableStaysReadableInGlassMode_data() {
        QTest::addColumn<QString>("component");
        for (const auto &name : {"horizontal-header", "vertical-header", "cells", "editor"})
            QTest::newRow(name) << QString(name);
    }
    void preparationTableStaysReadableInGlassMode() {
        QFETCH(QString, component);
        MainWindow window(nullptr, dataPath());
        window.findChild<QCheckBox *>("transparentBackground")->setChecked(true);
        PreparationDialog dialog(dataPath(), {}, false, &window);
        auto tabs = dialog.findChild<QTabWidget *>();
        auto table = dialog.findChild<QTableWidget *>();
        QVERIFY(tabs);
        QVERIFY(table);
        table->setRowCount(1);
        table->setItem(0, 0, new QTableWidgetItem("SPI"));
        tabs->setCurrentIndex(2);
        dialog.show();
        QTest::qWait(30);
        screenshot(dialog, "appearance-preparation-glass");
        QWidget *area = nullptr;
        QPoint background(2, 2);
        if (component == "horizontal-header")
            area = table->horizontalHeader()->viewport();
        else if (component == "vertical-header")
            area = table->verticalHeader()->viewport();
        else if (component == "cells") {
            area = table->viewport();
            background = QPoint(table->columnWidth(0) - 10, table->rowHeight(0) / 2);
        } else {
            table->editItem(table->item(0, 0));
            QCoreApplication::processEvents();
            area = table->findChild<QLineEdit *>();
            QVERIFY(area);
            background = QPoint(area->width() - 10, area->height() / 2);
        }
        QVERIFY(area);
        const auto image = area->grab().toImage();
        QVERIFY2(image.pixelColor(background).lightnessF() < 0.4,
                 "A terminology table component painted light behind light text");
        if (component == "vertical-header")
            QVERIFY2(image.pixelColor(2, image.height() - 3).lightnessF() < 0.4,
                     "The empty header viewport retained a light background");
        QVERIFY(area->palette().color(QPalette::WindowText).lightnessF() > 0.8);
    }
};
QTEST_MAIN(AppearanceTest)
#include "tst_appearance.moc"
