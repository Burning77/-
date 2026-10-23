#include "ResumeUploadWidget.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
ResumeUploadWidget::ResumeUploadWidget(QWidget *parent) : QWidget(parent) {
#ifdef Q_OS_WIN
    reader.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    setObjectName("resumeUpload");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto row = new QHBoxLayout;
    upload = new QPushButton(QStringLiteral("上传简历"));
    upload->setObjectName("uploadResumeButton");
    summary = new QLabel;
    summary->setObjectName("resumeSummary");
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    preview = new QPushButton(QStringLiteral("查看文字"));
    preview->setObjectName("previewResumeButton");
    remove = new QPushButton(QStringLiteral("移除简历"));
    remove->setObjectName("removeResumeButton");
    row->addWidget(upload);
    row->addWidget(summary, 1);
    row->addWidget(preview);
    row->addWidget(remove);
    layout->addLayout(row);
    status = new QLabel(
        QStringLiteral("支持 PDF、DOCX、TXT，最大 20 MB。AI 分析时会发送简历文字给当前所选服务。"));
    status->setObjectName("resumeStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    timeout.setSingleShot(true);
    timeout.setInterval(30000);
    connect(upload, &QPushButton::clicked, this, [this] {
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("上传简历"), {},
                                                       QStringLiteral("简历 (*.pdf *.docx *.txt)"));
        if (!file.isEmpty())
            importFile(file);
    });
    connect(preview, &QPushButton::clicked, this, &ResumeUploadWidget::showPreview);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (readOnly || importing)
            return;
        data = {};
        status->setText(QStringLiteral("简历已移除，请重新分析公司与岗位"));
        updateControls();
        emit documentChanged();
    });
    connect(&reader, &QProcess::readyReadStandardOutput, this, [this] {
        output += reader.readAllStandardOutput();
        if (output.size() > 256 * 1024) {
            importError = QStringLiteral("提取结果过大，请精简简历后上传");
            reader.kill();
        }
    });
    connect(&timeout, &QTimer::timeout, this, [this] {
        importError = QStringLiteral("简历读取超时，请检查文件或另存为 DOCX/TXT 后上传");
        reader.kill();
    });
    connect(&reader, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            timeout.stop();
            setBusy(false);
            status->setText(QStringLiteral("简历读取程序缺失或无法启动，请完整解压运行包"));
        }
    });
    connect(&reader, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exitStatus) {
        timeout.stop();
        output += reader.readAllStandardOutput();
        const auto value = QJsonDocument::fromJson(output).object();
        setBusy(false);
        if (importError.isEmpty())
            importError = value["error"].toString();
        if (!importError.isEmpty()) {
            status->setText(importError);
            return;
        }
        if (code != 0 || exitStatus != QProcess::NormalExit || value["text"].toString().trimmed().isEmpty() ||
            value["text"].toString().size() > 20000 || value["fileName"].toString().isEmpty()) {
            status->setText(QStringLiteral("简历读取失败，原简历已保留；请检查文件后重新上传"));
            return;
        }
        data = value;
        data["importedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        status->setText(QStringLiteral("简历已读取，可查看文字。请点击“AI 分析公司与岗位”结合简历分析。"));
        updateControls();
        emit documentChanged();
    });
    updateControls();
}
ResumeUploadWidget::~ResumeUploadWidget() {
    if (reader.state() != QProcess::NotRunning) {
        reader.disconnect(this);
        reader.kill();
        reader.waitForFinished(1000);
    }
}
void ResumeUploadWidget::setDocument(QJsonObject value, const QString &legacyExperience) {
    data = value;
    if (data.value("text").toString().isEmpty() && !legacyExperience.isEmpty())
        data = {{"fileName", QStringLiteral("旧版填写的经历（可上传简历替换）")},
                {"text", legacyExperience},
                {"legacy", true}};
    updateControls();
}
void ResumeUploadWidget::setReadOnly(bool value) {
    readOnly = value;
    updateControls();
}
void ResumeUploadWidget::setBusy(bool value) {
    importing = value;
    updateControls();
    emit busyChanged(value);
}
void ResumeUploadWidget::updateControls() {
    const auto hasText = !data.value("text").toString().isEmpty();
    summary->setText(hasText ? QStringLiteral("%1 · 已提取 %2 字")
                                   .arg(data.value("fileName").toString())
                                   .arg(data.value("text").toString().size())
                             : QStringLiteral("未上传简历"));
    upload->setText(hasText ? QStringLiteral("更换简历") : QStringLiteral("上传简历"));
    upload->setEnabled(!readOnly && !importing);
    remove->setEnabled(hasText && !readOnly && !importing);
    preview->setEnabled(hasText);
}
void ResumeUploadWidget::importFile(const QString &path) {
    if (readOnly || importing || path.isEmpty())
        return;
    output.clear();
    importError.clear();
    setBusy(true);
    status->setText(QStringLiteral("正在本机提取简历文字…"));
    reader.start(QCoreApplication::applicationDirPath() + "/ResumeReader.exe", {path});
    timeout.start();
}
void ResumeUploadWidget::showPreview() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("简历文字 · ") + data.value("fileName").toString());
    const auto available = dialog.screen()->availableGeometry();
    dialog.resize(qMin(720, available.width() * 85 / 100), qMin(560, available.height() * 85 / 100));
    auto layout = new QVBoxLayout(&dialog);
    auto text = new QPlainTextEdit(data.value("text").toString());
    text->setObjectName("resumeTextPreview");
    text->setReadOnly(true);
    layout->addWidget(text);
    auto close = new QPushButton(QStringLiteral("关闭"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(close);
    dialog.exec();
}
