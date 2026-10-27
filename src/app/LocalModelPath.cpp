#include "LocalModelPath.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

QString resolveLocalModelPath(const QString &configuredPath, const QString &applicationDirectory) {
    const auto bundled = QDir(applicationDirectory).filePath("local-asr/bilingual");
    if (configuredPath.trimmed().isEmpty())
        return bundled;

    const QFileInfo configured(QDir::cleanPath(QDir::fromNativeSeparators(configuredPath)));
    const auto modelParent = configured.dir();
    if (configured.fileName().compare("bilingual", Qt::CaseInsensitive) != 0 ||
        modelParent.dirName().compare("local-asr", Qt::CaseInsensitive) != 0)
        return configuredPath;

    // Only relocate our versioned bundle defaults. Custom model directories,
    // including invalid ones, remain visible for the user to correct.
    static const QRegularExpression releaseName(
        "^InterviewAssistant(?:-\\d+\\.\\d+\\.\\d+(?:[-._][A-Za-z0-9]+)*)?$",
        QRegularExpression::CaseInsensitiveOption);
    if (!releaseName.match(QFileInfo(modelParent.path()).dir().dirName()).hasMatch())
        return configuredPath;

    for (const auto &name : {"encoder-epoch-99-avg-1.int8.onnx", "decoder-epoch-99-avg-1.onnx",
                             "joiner-epoch-99-avg-1.int8.onnx", "tokens.txt"}) {
        const QFileInfo file(QDir(bundled).filePath(name));
        if (!file.isFile() || file.size() <= 0)
            return configuredPath;
    }
    return bundled;
}
