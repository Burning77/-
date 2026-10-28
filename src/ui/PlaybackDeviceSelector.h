#pragma once
#include "audio/WasapiLoopbackSource.h"
#include <QComboBox>

void populatePlaybackDevices(QComboBox &combo, const QList<RenderDevice> &devices,
                             const QString &preferredId);
