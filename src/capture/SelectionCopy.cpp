#include "SelectionCopy.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
SelectionCopy::SelectionCopy(QObject *parent, SelectionBackend value)
    : QObject(parent), backend(std::move(value)) {
#ifdef Q_OS_WIN
    if (!backend.foreground)
        backend.foreground = [] { return quintptr(GetForegroundWindow()); };
    if (!backend.external)
        backend.external = [](quintptr id) {
            DWORD process = 0;
            return id && GetWindowThreadProcessId(reinterpret_cast<HWND>(id), &process) && process &&
                   process != GetCurrentProcessId();
        };
    if (!backend.released)
        backend.released = [] {
            for (int key : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN})
                if (GetAsyncKeyState(key) & 0x8000)
                    return false;
            return true;
        };
    if (!backend.clipboardRevision)
        backend.clipboardRevision = [] { return quint64(GetClipboardSequenceNumber()); };
    if (!backend.clipboardMatchesTarget)
        backend.clipboardMatchesTarget = [](quintptr id) {
            DWORD targetProcess = 0, ownerProcess = 0;
            const auto owner = GetClipboardOwner();
            return id && owner && GetWindowThreadProcessId(reinterpret_cast<HWND>(id), &targetProcess) &&
                   GetWindowThreadProcessId(owner, &ownerProcess) && targetProcess &&
                   targetProcess == ownerProcess;
        };
    if (!backend.copy)
        backend.copy = [] {
            INPUT events[4]{};
            for (auto &event : events)
                event.type = INPUT_KEYBOARD;
            events[0].ki.wVk = VK_CONTROL;
            events[1].ki.wVk = 'C';
            events[2].ki.wVk = 'C';
            events[2].ki.dwFlags = KEYEVENTF_KEYUP;
            events[3].ki.wVk = VK_CONTROL;
            events[3].ki.dwFlags = KEYEVENTF_KEYUP;
            const auto sent = SendInput(4, events, sizeof(INPUT));
            if (sent != 4)
                SendInput(2, events + 2, sizeof(INPUT));
            return sent == 4;
        };
#else
    if (!backend.foreground)
        backend.foreground = [] { return quintptr(0); };
    if (!backend.external)
        backend.external = [](quintptr) { return false; };
    if (!backend.released)
        backend.released = [] { return true; };
    if (!backend.clipboardRevision)
        backend.clipboardRevision = [] { return quint64(0); };
    if (!backend.clipboardMatchesTarget)
        backend.clipboardMatchesTarget = [](quintptr) { return false; };
    if (!backend.copy)
        backend.copy = [] { return false; };
#endif
    if (!backend.readText)
        backend.readText = [this](quint64 revision, SelectionTextCallback done) {
            readClipboard(revision, std::move(done));
        };
    poll.setInterval(20);
    connect(&poll, &QTimer::timeout, this, &SelectionCopy::step);
}
SelectionCopy::~SelectionCopy() {
    cancel();
}
void SelectionCopy::start() {
    cancel();
    target = backend.foreground();
    if (!backend.external(target)) {
        finish({}, QStringLiteral("请先在其他程序中选中题目文字，再按取词快捷键"));
        return;
    }
    copied = reading = false;
    elapsed.start();
    poll.start();
}
void SelectionCopy::cancel() {
    ++serial;
    poll.stop();
    if (reader)
        reader->kill();
    reader.clear();
}
void SelectionCopy::step() {
    if (backend.foreground() != target) {
        finish({}, QStringLiteral("取词已取消：前台窗口发生变化"));
        return;
    }
    if (elapsed.elapsed() > 4000) {
        finish({}, QStringLiteral("未获取到本次选中文字；请先选中题目，或手动复制后粘贴"));
        return;
    }
    if (reading)
        return;
    if (!copied) {
        if (!backend.released())
            return;
        baseline = backend.clipboardRevision();
        if (!backend.copy()) {
            finish({}, QStringLiteral("无法向目标程序发送复制操作；请检查权限或手动复制"));
            return;
        }
        copied = true;
        return;
    }
    const auto revision = backend.clipboardRevision();
    if (revision == baseline || !revision)
        return;
    if (!backend.clipboardMatchesTarget(target)) {
        finish({}, QStringLiteral("取词已取消：无法确认剪贴板文字来自所选程序，请手动复制粘贴"));
        return;
    }
    reading = true;
    const auto epoch = serial;
    const QPointer<SelectionCopy> self(this);
    backend.readText(revision, [self, epoch](SelectionTextResult result) {
        if (!self || self->serial != epoch)
            return;
        if (self->backend.foreground() != self->target ||
            !self->backend.clipboardMatchesTarget(self->target)) {
            self->finish({}, QStringLiteral("取词已取消：目标窗口或剪贴板已变化"));
            return;
        }
        if (result.retryable) {
            self->reading = false; // Retry the read only; never send Ctrl+C again.
            return;
        }
        if (!result.error.isEmpty())
            self->finish({}, result.error);
        else if (!result.revision || result.revision == self->baseline ||
                 self->backend.clipboardRevision() != result.revision)
            self->finish({}, QStringLiteral("取词已取消：读取后的剪贴板已变化"));
        else if (result.text.trimmed().isEmpty())
            self->finish({}, QStringLiteral("本次复制没有文字，请重新选择题目"));
        else
            self->finish(result.text.trimmed(), {});
    });
}
void SelectionCopy::finish(QString text, QString error) {
    cancel();
    emit finished(text, error);
}
void SelectionCopy::readClipboard(quint64 revision, SelectionTextCallback done) {
    quint32 expectedProcess = 0;
#ifdef Q_OS_WIN
    DWORD processId = 0;
    if (target)
        GetWindowThreadProcessId(reinterpret_cast<HWND>(target), &processId);
    expectedProcess = processId;
#endif
    if (!expectedProcess) {
        done({{}, QStringLiteral("无法确认取词程序，请手动复制粘贴")});
        return;
    }
    auto process = new QProcess(this);
    reader = process;
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(
        process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [process, done](int code, QProcess::ExitStatus status) {
            const auto output = process->readAllStandardOutput();
            process->deleteLater();
            if (status == QProcess::NormalExit && code == 3)
                done({{}, {}, 0, true});
            else if (code != 0 || status != QProcess::NormalExit || output.size() > 60000)
                done({{},
                      QStringLiteral("本次选中文字读取失败（组件代码 %1）；请重新选择或手动粘贴").arg(code)});
            else {
                const auto split = output.indexOf('\n');
                bool valid = false;
                const auto completedRevision = output.left(split).toULongLong(&valid);
                if (split <= 0 || !valid || !completedRevision)
                    done({{}, QStringLiteral("取词组件返回无效数据，请保留完整运行包")});
                else
                    done({QString::fromUtf8(output.mid(split + 1)), {}, completedRevision});
            }
        });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            done({{}, QStringLiteral("无法启动取词组件，请保留完整运行包或手动复制题目")});
        }
    });
    process->start(QCoreApplication::applicationDirPath() + "/ClipboardReader.exe",
                   {QString::number(revision), QString::number(expectedProcess)});
}
