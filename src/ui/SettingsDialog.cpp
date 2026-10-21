#include "SettingsDialog.h"
#include "app/CredentialStore.h"
#include "transcription/RealtimeTranscriber.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
SettingsDialog::SettingsDialog(QWidget *p) : QDialog(p), client(this) {
    setWindowTitle(QStringLiteral("模型与转写设置"));
    resize(760, 510);
    auto layout = new QVBoxLayout(this);
    auto row = new QFormLayout;
    selected = new QComboBox;
    selected->addItems({"OpenAI", "DeepSeek"});
    selected->setCurrentText(AppSettings().textConfig().provider);
    row->addRow(QStringLiteral("当前文本服务"), selected);
    layout->addLayout(row);
    auto tabs = new QTabWidget;
    layout->addWidget(tabs);
    for (auto name : QStringList{"OpenAI", "DeepSeek", "Transcription"}) {
        auto config = name == "Transcription" ? AppSettings().transcription() : AppSettings().config(name);
        auto page = new QWidget;
        auto form = new QFormLayout(page);
        Fields f{name, new QLineEdit(config.baseUrl), new QLineEdit(config.model), new QLineEdit, new QLabel};
        f.key->setEchoMode(QLineEdit::Password);
        f.key->setPlaceholderText(CredentialStore::read(config.credentialId()).isEmpty()
                                      ? QStringLiteral("填写该服务的 API Key")
                                      : QStringLiteral("已保存；留空保留现有密钥"));
        f.model->setPlaceholderText(QStringLiteral("填写账户可用的 API 模型 ID"));
        f.result->setWordWrap(true);
        form->addRow(QStringLiteral("API 地址"), f.url);
        form->addRow(QStringLiteral("模型名称"), f.model);
        form->addRow("API Key", f.key);
        auto testButton = new QPushButton(QStringLiteral("保存并测试连接"));
        int index = fields.size();
        connect(testButton, &QPushButton::clicked, this, [this, index] { test(index); });
        form->addRow(testButton);
        auto clearButton = new QPushButton(QStringLiteral("清除该地址的已保存密钥"));
        connect(clearButton, &QPushButton::clicked, this, [this, index] {
            auto &f = fields[index];
            ProviderConfig c;
            c.provider = f.provider;
            c.baseUrl = f.url->text().trimmed();
            if (CredentialStore::remove(c.credentialId())) {
                f.key->clear();
                f.key->setPlaceholderText(QStringLiteral("尚未保存密钥"));
                f.result->setText(QStringLiteral("密钥已清除"));
            } else
                f.result->setText(QStringLiteral("密钥清除失败"));
        });
        form->addRow(clearButton);
        form->addRow(f.result);
        auto help = new QLabel(
            name == "Transcription"
                ? QStringLiteral(
                      "语音转写使用独立凭据。默认 "
                      "gpt-live-transcribe；更换模型需确认其支持当前实时转写协议。音频将发送到此服务。")
                : QStringLiteral("参考回答、问题提取和复盘使用当前选定的文本服务。切换只影响后续请求。密钥保"
                                 "存在 Windows 凭据管理器。"));
        help->setWordWrap(true);
        form->addRow(help);
        fields.append(f);
        tabs->addTab(page, name == "Transcription" ? QStringLiteral("语音转写") : name);
    }
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
bool SettingsDialog::save() {
    for (auto f : fields) {
        ProviderConfig c;
        c.provider = f.provider;
        c.baseUrl = f.url->text().trimmed();
        c.model = f.model->text().trimmed();
        QUrl u(c.baseUrl);
        if (!u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() ||
            u.scheme() != (f.provider == "Transcription" ? "wss" : "https")) {
            f.result->setText(QStringLiteral("请填写有效且不带用户名密码的 HTTPS/WSS 地址"));
            return false;
        }
        auto key = f.key->text().trimmed();
        if (key.contains('\r') || key.contains('\n')) {
            f.result->setText(QStringLiteral("密钥不能包含换行符"));
            return false;
        }
        if (!key.isEmpty() && !CredentialStore::write(c.credentialId(), key)) {
            f.result->setText(QStringLiteral("Windows 凭据保存失败"));
            return false;
        }
        if (f.provider == "Transcription")
            AppSettings().saveTranscription(c);
        else
            AppSettings().save(c);
        f.key->clear();
        f.key->setPlaceholderText(CredentialStore::read(c.credentialId()).isEmpty()
                                      ? QStringLiteral("填写 API Key")
                                      : QStringLiteral("已保存；留空保留"));
    }
    AppSettings().select(selected->currentText());
    return true;
}
void SettingsDialog::test(int index) {
    if (!save())
        return;
    auto f = fields[index];
    f.result->setText(QStringLiteral("正在连接…"));
    if (f.provider == "Transcription") {
        auto transcriber = new RealtimeTranscriber(this);
        auto complete = std::make_shared<bool>(false);
        connect(transcriber, &RealtimeTranscriber::status, this,
                [f, transcriber, complete](QString, QString message) {
                    f.result->setText(message);
                    if (message == QStringLiteral("正在实时转写")) {
                        *complete = true;
                        f.result->setText(QStringLiteral("转写配置已被服务确认"));
                        transcriber->stop();
                        transcriber->deleteLater();
                    }
                });
        transcriber->connectSession(newId(), "test", AppSettings().transcription());
        QTimer::singleShot(12000, this, [f, transcriber, complete] {
            if (!*complete) {
                *complete = true;
                f.result->setText(QStringLiteral("未完成连接测试，请检查配置和网络"));
                transcriber->stop();
                transcriber->deleteLater();
            }
        });
        return;
    }
    TextRequest r;
    r.config = AppSettings().config(f.provider);
    r.operation = "test";
    r.messages = {QJsonObject{{"role", "user"}, {"content", "Reply with OK."}}};
    auto id = r.id;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(&client, &TextClient::finished, this, [f, id, connection](QString rid) {
        if (rid == id) {
            f.result->setText(QStringLiteral("连接成功，模型已正常返回"));
            QObject::disconnect(*connection);
        }
    });
    auto failure = std::make_shared<QMetaObject::Connection>();
    *failure =
        connect(&client, &TextClient::failed, this, [f, id, failure, connection](QString rid, QString error) {
            if (rid == id) {
                f.result->setText(error);
                QObject::disconnect(*failure);
                QObject::disconnect(*connection);
            }
        });
    client.start(r);
}
