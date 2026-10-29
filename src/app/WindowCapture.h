#pragma once
#include <QString>
#include <functional>

struct WindowCaptureResult {
    bool ok = false;
    QString error;
};
using WindowCaptureBackend = std::function<WindowCaptureResult(quintptr, bool)>;
namespace WindowCapture {
bool supportedVersion(int major, int build);
QString supportError();
WindowCaptureResult setExcluded(quintptr window, bool excluded);
WindowCaptureResult setTaskbarExcluded(quintptr window, bool excluded);
quint32 taskbarButtonCreatedMessage();
bool isTaskbarButtonCreatedMessage(void *message, quintptr window);
} // namespace WindowCapture
