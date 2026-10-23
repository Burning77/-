#include "ui/MainWindow.h"
#include "app/InstanceGuard.h"
#include "app/NetworkRuntime.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("LocalInterviewTools");
    app.setApplicationName("InterviewAssistant");
    app.setApplicationVersion(INTERVIEW_APP_VERSION);
    NetworkRuntime::initializeTls();
    const auto profileRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(profileRoot);
    InstanceGuard instance(profileRoot + "/application.lock");
    if (!instance.acquire()) {
        QMessageBox::information(nullptr, QStringLiteral("面试助手已在运行"),
                                 QStringLiteral("请使用已打开的面试助手窗口；同一账户仅运行一个实例。若没有已"
                                                "打开窗口，请检查应用数据目录的写入权限。"));
        return 2;
    }
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt 面试记录与复盘助手"));
    parser.addHelpOption();
    parser.addOption({"data-dir", QStringLiteral("指定会话数据目录"), "directory"});
    parser.process(app);
    MainWindow window(nullptr, parser.value("data-dir"));
    window.show();
    return app.exec();
}
