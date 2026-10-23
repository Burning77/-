#include "ResumeDocument.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2)
        return 2;
    const auto result = ResumeDocument::extract(app.arguments().at(1));
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly))
        return 2;
    output.write(QJsonDocument(result).toJson(QJsonDocument::Compact));
    output.flush();
    return result.contains("error") ? 1 : 0;
}
