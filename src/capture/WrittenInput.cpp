#include "WrittenInput.h"
#include "RegionSelector.h"
#include "CaptureShortcuts.h"
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif
WrittenInput::WrittenInput(QWidget *widget, HotkeyBackend keys, SelectionBackend selected,
                           WindowCaptureBackend protect, RegionGrabber capture)
    : QObject(widget), owner(widget), copyKey(this, keys), regionKey(this, std::move(keys)),
      selection(this, std::move(selected)), protection(std::move(protect)), grabber(std::move(capture)) {
    if (!grabber)
        grabber = [](QScreen *screen, QRect area) {
            const auto local = area.translated(-screen->geometry().topLeft());
            return screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
        };
    connect(&copyKey, &GlobalHotkey::activated, this, &WrittenInput::captureSelection);
    connect(&regionKey, &GlobalHotkey::activated, this, &WrittenInput::captureRegion);
    auto completed = [this](QString text, QString error) {
        if (!running)
            return;
        if (!error.isEmpty())
            emit status(error);
        else
            emit questionCaptured(text);
    };
    connect(&selection, &SelectionCopy::finished, this, completed);
}
WrittenInput::~WrittenInput() {
    stop();
}
void WrittenInput::start() {
    if (running)
        return;
    running = true;
    const auto copy = CaptureShortcuts::selection(), shot = CaptureShortcuts::region();
    const auto boss = QKeySequence::fromString(
        QSettings()
            .value("shortcuts/bossKey", GlobalHotkey::defaultSequence().toString(QKeySequence::PortableText))
            .toString(),
        QKeySequence::PortableText);
    const auto valid = CaptureShortcuts::validate(copy, shot, boss);
    if (!valid.ok) {
        const QPointer<WrittenInput> self(this);
        emit shortcutStatus(valid.error + QStringLiteral("；请返回原窗口设置快捷键"), false);
        if (!self)
            return;
        emit status(valid.error + QStringLiteral("；请返回原窗口设置快捷键"));
        return;
    }
    const auto copyResult = copyKey.setSequence(copy), shotResult = regionKey.setSequence(shot);
    QStringList errors;
    if (!copyResult.ok)
        errors << QStringLiteral("取词快捷键未生效：%1").arg(copyResult.error);
    if (!shotResult.ok)
        errors << QStringLiteral("截图快捷键未生效：%1").arg(shotResult.error);
    const QPointer<WrittenInput> self(this);
    emit shortcutStatus(
        QStringLiteral("%1 取词：%2 · %3 截图：%4。文字/截图会发送给所选模型；返回原窗口可改键。")
            .arg(copy.toString(QKeySequence::NativeText),
                 copyResult.ok ? QStringLiteral("已就绪") : copyResult.error,
                 shot.toString(QKeySequence::NativeText),
                 shotResult.ok ? QStringLiteral("已就绪") : shotResult.error),
        errors.isEmpty());
    if (!self)
        return;
    if (!errors.isEmpty())
        emit status(errors.join(QStringLiteral("；")));
}
void WrittenInput::stop() {
    running = false;
    cancelCapture();
    copyKey.clear();
    regionKey.clear();
}
void WrittenInput::captureSelection() {
    if (!running)
        return;
    cancelCapture();
    emit status(QStringLiteral("正在获取本次选中文字…"));
    selection.start();
}
void WrittenInput::captureRegion() {
    if (!running || !owner)
        return;
    cancelCapture();
    const auto epoch = serial;
    QPointer<QScreen> screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen) {
        emit status(QStringLiteral("没有可截图的显示器"));
        return;
    }
    auto tool = new RegionSelector(screen->geometry(), owner, protection);
    const QPointer<WrittenInput> self(this);
    const QPointer<RegionSelector> toolGuard(tool);
    selector = tool;
    connect(tool, &RegionSelector::cancelled, this, [this, tool] {
        if (selector == tool)
            selector.clear();
        tool->deleteLater();
        emit status(QStringLiteral("已取消区域截图"));
    });
    connect(tool, &RegionSelector::selected, this, [this, tool, screen, epoch](QRect region) {
        if (selector == tool)
            selector.clear();
        tool->deleteLater();
        emit status(QStringLiteral("正在获取所选区域图片…"));
        QTimer::singleShot(40, this, [this, screen, region, epoch] {
            if (!running || serial != epoch)
                return;
            if (!screen) {
                emit status(QStringLiteral("截图显示器已断开，请重试"));
                return;
            }
#ifdef Q_OS_WIN
            if (QGuiApplication::platformName() == "windows")
                DwmFlush();
#endif
            const auto image = grabber(screen, region);
            if (image.isNull()) {
                emit status(QStringLiteral("未获取到区域图片，请重试"));
                return;
            }
            emit imageCaptured(image);
        });
    });
    QString error;
    const bool started = tool->start(&error);
    if (!self)
        return;
    if (!toolGuard) {
        selector.clear();
        emit status(QStringLiteral("截图选框已关闭"));
        return;
    }
    if (!started) {
        selector.clear();
        tool->deleteLater();
        emit status(error);
    }
}
void WrittenInput::cancelCapture() {
    ++serial;
    selection.cancel();
    if (selector) {
        selector->hide();
        selector->deleteLater();
        selector.clear();
    }
}
