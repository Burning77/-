#include "ui/MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("LocalInterviewTools");
    app.setApplicationName("InterviewAssistant");
    app.setApplicationVersion(INTERVIEW_APP_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt 面试记录与复盘助手"));
    parser.addHelpOption();
    parser.addOption({"data-dir", QStringLiteral("指定会话数据目录"), "directory"});
    parser.process(app);
    MainWindow window(nullptr, parser.value("data-dir"));
    window.show();
    return app.exec();
}
