#include "PlaybackDeviceSelector.h"
#include <QSignalBlocker>

void populatePlaybackDevices(QComboBox &combo, const QList<RenderDevice> &devices,
                             const QString &preferredId) {
    const QSignalBlocker blocker(combo);
    combo.clear();
    // An empty ID is resolved by WASAPI to the system playback endpoint when
    // recording starts. Enumeration order does not identify that endpoint.
    combo.addItem(QStringLiteral("系统默认播放设备（推荐）"), QString());
    for (const auto &device : devices)
        combo.addItem(device.name, device.id);
    const auto preferredIndex = combo.findData(preferredId);
    combo.setCurrentIndex(preferredIndex >= 0 ? preferredIndex : 0);
}
