#include "SettingsDialog.h"
#include "app/CredentialStore.h"
#include "transcription/RealtimeTranscriber.h"
#include "transcription/LocalTranscriber.h"
#include "auth/ChatGptAccount.h"
#include "auth/DeepSeekAccount.h"
#include <QDesktopServices>
#include <QPointer>
#include <QScrollArea>
#include <QSettings>
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
    selected->addItems({"ChatGPT", "OpenAI", "DeepSeek"});
    selected->setCurrentText(AppSettings().textConfig().provider);
    row->addRow(QStringLiteral("当前文本服务"), selected);
    layout->addLayout(row);
    auto tabs = new QTabWidget;
    layout->addWidget(tabs);
    auto addTab = [tabs](QWidget *page, QString title) {
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
    };
    auto planPage = new QWidget;
    auto planForm = new QFormLayout(planPage);
    accounts = new QComboBox;
    accounts->setObjectName("chatgptAccount");
    planModels = new QComboBox;
    planModels->setObjectName("chatgptModel");
    planStatus = new QLabel;
    planStatus->setWordWrap(true);
    planStatus->setTextFormat(Qt::PlainText);
    auto login = new QPushButton(QStringLiteral("使用 ChatGPT 登录 · Continue with ChatGPT"));
    login->setObjectName("chatgptLoginButton");
    auto addAccount = new QPushButton(QStringLiteral("添加其他 ChatGPT 账户 / 工作区"));
    auto logout = new QPushButton(QStringLiteral("退出所选账户"));
    auto modelRefresh = new QPushButton(QStringLiteral("刷新可用模型"));
    auto testPlan = new QPushButton(QStringLiteral("保存并测试 ChatGPT 套餐"));
    auto usage = new QPushButton(QStringLiteral("管理 ChatGPT 套餐用量"));
    planForm->addRow(QStringLiteral("ChatGPT 账户"), accounts);
    planForm->addRow(login);
    planForm->addRow(addAccount);
    planForm->addRow(logout);
    planForm->addRow(QStringLiteral("可用模型"), planModels);
    planForm->addRow(modelRefresh);
    planForm->addRow(testPlan);
    planForm->addRow(usage);
    planForm->addRow(planStatus);
    auto planHelp =
        new QLabel(QStringLiteral("登录并授权后，问题提取、参考回答和复盘使用你的 ChatGPT "
                                  "套餐，仍受账户及本工具的用量限制。语音转写请选择本地识别或独立服务。"));
    planHelp->setWordWrap(true);
    planForm->addRow(planHelp);
    addTab(planPage, QStringLiteral("ChatGPT 套餐"));
    connect(login, &QPushButton::clicked, this,
            [this] { ChatGptAccount::shared().login(accounts->currentData().toString()); });
    connect(addAccount, &QPushButton::clicked, this, [] { ChatGptAccount::shared().login(); });
    connect(logout, &QPushButton::clicked, this,
            [this] { ChatGptAccount::shared().signOut(accounts->currentData().toString()); });
    connect(usage, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl("https://chatgpt.com/settings/usage")); });
    connect(accounts, &QComboBox::currentIndexChanged, this,
            [this] { ChatGptAccount::shared().selectProfile(accounts->currentData().toString()); });
    connect(modelRefresh, &QPushButton::clicked, this, &SettingsDialog::loadPlanModels);
    connect(testPlan, &QPushButton::clicked, this, [this] {
        if (!save())
            return;
        TextRequest r;
        r.config = AppSettings().config("ChatGPT");
        r.operation = "test";
        r.messages = {QJsonObject{{"role", "user"}, {"content", "Reply with OK."}}};
        planStatus->setText(QStringLiteral("正在测试 ChatGPT 套餐…"));
        auto id = r.id;
        connect(&client, &TextClient::finished, this, [this, id](QString rid) {
            if (rid == id)
                planStatus->setText(QStringLiteral("ChatGPT 套餐连接成功"));
        });
        connect(&client, &TextClient::failed, this, [this, id](QString rid, QString error) {
            if (rid == id)
                planStatus->setText(error);
        });
        client.start(r);
    });
    connect(&ChatGptAccount::shared(), &ChatGptAccount::status, planStatus, &QLabel::setText);
    connect(&ChatGptAccount::shared(), &ChatGptAccount::changed, this, [this] {
        reloadAccounts();
        for (auto v : ChatGptAccount::shared().profiles())
            if (v.toObject()["clientId"] == ChatGptAccount::shared().activeProfile() &&
                v.toObject()["planEnabled"].toBool()) {
                selected->setCurrentText("ChatGPT");
                AppSettings().select("ChatGPT");
                if (!QSettings().value("chatgpt/welcomeShown").toBool()) {
                    QSettings().setValue("chatgpt/welcomeShown", true);
                    QMessageBox::information(
                        this, QStringLiteral("已启用 ChatGPT 套餐"),
                        QStringLiteral("本工具中符合条件的文本请求将使用你的 ChatGPT 套餐。你可在 ChatGPT "
                                       "设置中查看用量并管理本工具的限额。"));
                }
            }
        loadPlanModels();
    });
    reloadAccounts();
    for (auto name : QStringList{"OpenAI", "DeepSeek", "Transcription"}) {
        auto config = name == "Transcription" ? AppSettings().transcription() : AppSettings().config(name);
        auto page = new QWidget;
        auto form = new QFormLayout(page);
        Fields f{name, new QLineEdit(config.baseUrl), new QLineEdit(config.model), new QLineEdit, new QLabel};
        if (name == "DeepSeek")
            f.url->setText(QSettings().value("DeepSeek/url", "https://api.deepseek.com").toString());
        f.key->setEchoMode(QLineEdit::Password);
        f.key->setPlaceholderText(CredentialStore::read(config.credentialId()).isEmpty()
                                      ? QStringLiteral("填写该服务的 API Key")
                                      : QStringLiteral("已保存；留空保留现有密钥"));
        f.model->setPlaceholderText(QStringLiteral("填写账户可用的 API 模型 ID"));
        f.result->setWordWrap(true);
        f.result->setTextFormat(Qt::PlainText);
        if (name == "DeepSeek") {
            deepSeekMode = new QComboBox;
            deepSeekMode->setObjectName("deepSeekMode");
            deepSeekMode->addItem(QStringLiteral("DeepSeek 账号登录"), true);
            deepSeekMode->addItem(QStringLiteral("API Key"), false);
            deepSeekMode->setCurrentIndex(config.accountLogin ? 0 : 1);
            auto login = new QPushButton(QStringLiteral("使用 DeepSeek 账号登录"));
            login->setObjectName("deepSeekLoginButton");
            auto logout = new QPushButton(QStringLiteral("退出 DeepSeek 账号"));
            auto cancel = new QPushButton(QStringLiteral("取消等待登录"));
            auto refresh = new QPushButton(QStringLiteral("读取账号可用模型"));
            deepSeekStatus =
                new QLabel(DeepSeekAccount::shared().signedIn() ? QStringLiteral("DeepSeek 账号已登录")
                                                                : QStringLiteral("尚未登录 DeepSeek 账号"));
            deepSeekStatus->setWordWrap(true);
            deepSeekStatus->setTextFormat(Qt::PlainText);
            form->addRow(QStringLiteral("连接方式"), deepSeekMode);
            form->addRow(login);
            form->addRow(logout);
            form->addRow(cancel);
            form->addRow(refresh);
            form->addRow(deepSeekStatus);
            auto update = [this, f] {
                bool account = deepSeekMode->currentData().toBool();
                f.url->setEnabled(!account);
                f.key->setEnabled(!account);
            };
            connect(deepSeekMode, &QComboBox::currentIndexChanged, this, update);
            update();
            connect(login, &QPushButton::clicked, this, [this] {
                deepSeekMode->setCurrentIndex(0);
                DeepSeekAccount::shared().login();
            });
            connect(logout, &QPushButton::clicked, this, [] { DeepSeekAccount::shared().signOut(); });
            connect(cancel, &QPushButton::clicked, this, [this] {
                DeepSeekAccount::shared().cancelLogin();
                deepSeekStatus->setText(QStringLiteral("已取消等待登录"));
            });
            connect(refresh, &QPushButton::clicked, this, &SettingsDialog::loadDeepSeekModels);
            connect(&DeepSeekAccount::shared(), &DeepSeekAccount::status, deepSeekStatus, &QLabel::setText);
            connect(&DeepSeekAccount::shared(), &DeepSeekAccount::changed, this, [this] {
                if (DeepSeekAccount::shared().signedIn()) {
                    deepSeekMode->setCurrentIndex(0);
                    selected->setCurrentText("DeepSeek");
                    AppSettings().select("DeepSeek");
                    loadDeepSeekModels();
                } else
                    deepSeekStatus->setText(
                        QStringLiteral("DeepSeek 账号未登录，请重新登录或手动切换 API Key"));
            });
        }
        if (name == "Transcription") {
            speechMode = new QComboBox;
            speechMode->setObjectName("transcriptionMode");
            speechMode->addItem(QStringLiteral("本地中英混合识别（无需 API Key）"), "Local");
            speechMode->addItem(QStringLiteral("OpenAI 云端实时转写"), "Cloud");
            speechMode->setCurrentIndex(AppSettings().transcriptionMode() == "Local" ? 0 : 1);
            localModel = new QLineEdit(AppSettings().localModelPath());
            localModel->setObjectName("localModelPath");
            form->addRow(QStringLiteral("识别方式"), speechMode);
            form->addRow(QStringLiteral("本地模型目录"), localModel);
            auto update = [this, f] {
                bool cloud = speechMode->currentData() == "Cloud";
                f.url->setEnabled(cloud);
                f.model->setEnabled(cloud);
                f.key->setEnabled(cloud);
                localModel->setEnabled(!cloud);
            };
            connect(speechMode, &QComboBox::currentIndexChanged, this, update);
            update();
        }
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
            name == "DeepSeek" ? QStringLiteral("账号登录会在默认浏览器打开 DeepSeek "
                                                "官方平台授权页。调用权限和额度以该账号为准；登录不代表免费聊"
                                                "天额度可用于本软件。API Key 模式可独立使用。")
            : name == "Transcription"
                ? QStringLiteral("本地模式支持中英混说，英文按原文显示；音频留在本机，无需 "
                                 "Key。云端模式使用独立 API 凭据和额度，默认 "
                                 "gpt-live-transcribe；音频会发送给云端服务。设置在下一场录音开始时生效。")
                : QStringLiteral("参考回答、问题提取和复盘使用当前选定的文本服务。切换只影响后续请求。密钥保"
                                 "存在 Windows 凭据管理器。"));
        help->setWordWrap(true);
        form->addRow(help);
        fields.append(f);
        addTab(page, name == "Transcription" ? QStringLiteral("语音转写") : name);
    }
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (!ChatGptAccount::shared().activeProfile().isEmpty())
        loadPlanModels();
    if (deepSeekMode->currentData().toBool() && DeepSeekAccount::shared().signedIn())
        loadDeepSeekModels();
}
void SettingsDialog::loadDeepSeekModels() {
    auto session = DeepSeekAccount::shared().sessionId();
    if (session.isEmpty()) {
        deepSeekStatus->setText(QStringLiteral("请先登录 DeepSeek 账号"));
        return;
    }
    deepSeekStatus->setText(QStringLiteral("正在读取账号可用模型…"));
    QPointer<SettingsDialog> guard(this);
    DeepSeekAccount::shared().models([guard, session](QJsonObject result, QString error) {
        if (!guard || session != DeepSeekAccount::shared().sessionId())
            return;
        if (!error.isEmpty()) {
            guard->deepSeekStatus->setText(error);
            return;
        }
        QStringList ids;
        for (auto value : result["data"].toArray()) {
            auto model = value.toObject();
            auto id = model["id"].toString();
            auto input = model.value("input_modalities").toArray(QJsonArray{"text"});
            auto output = model.value("output_modalities").toArray(QJsonArray{"text"});
            if (!id.trimmed().isEmpty() && input.contains("text") && output.contains("text") &&
                model["api_capabilities"].toObject()["anthropic_messages"].isObject() && !ids.contains(id))
                ids.append(id);
        }
        if (ids.isEmpty()) {
            guard->deepSeekStatus->setText(
                QStringLiteral("账号未返回支持文本对话的可用模型，请检查平台授权"));
            return;
        }
        if (!guard->deepSeekMode->currentData().toBool())
            return;
        for (auto f : guard->fields)
            if (f.provider == "DeepSeek") {
                auto model = f.model->text().trimmed();
                if (!ids.contains(model))
                    model = ids.contains("deepseek-flash") ? "deepseek-flash" : ids.first();
                f.model->setText(model);
                auto config = AppSettings().config("DeepSeek");
                config.accountLogin = true;
                config.model = model;
                AppSettings().save(config);
            }
        guard->deepSeekStatus->setText(QStringLiteral("DeepSeek 账号已连接；可用模型：") + ids.join("、"));
    });
}
void SettingsDialog::reloadAccounts() {
    QSignalBlocker block(accounts);
    accounts->clear();
    for (auto value : ChatGptAccount::shared().profiles()) {
        auto p = value.toObject();
        accounts->addItem(p["label"].toString() + " · " + p["clientId"].toString().right(6),
                          p["clientId"].toString());
    }
    int i = accounts->findData(ChatGptAccount::shared().activeProfile());
    accounts->setCurrentIndex(i >= 0 ? i : accounts->count() ? 0 : -1);
    if (ChatGptAccount::shared().activeProfile().isEmpty())
        planStatus->setText(QStringLiteral("尚未登录 ChatGPT，点击登录并授权使用套餐"));
}
void SettingsDialog::loadPlanModels() {
    auto account = ChatGptAccount::shared().activeProfile();
    if (account.isEmpty()) {
        planModels->clear();
        return;
    }
    QPointer<SettingsDialog> guard(this);
    planStatus->setText(QStringLiteral("正在读取当前账户可用的模型…"));
    ChatGptAccount::shared().models(account, [guard, account](QJsonObject result, QString error) {
        if (!guard || ChatGptAccount::shared().activeProfile() != account)
            return;
        if (!error.isEmpty()) {
            guard->planStatus->setText(error);
            return;
        }
        auto previous = AppSettings().config("ChatGPT").model;
        guard->planModels->clear();
        for (auto value : result["models"].toArray()) {
            auto m = value.toObject();
            if (m["visibility"] == "list" && !m["slug"].toString().isEmpty())
                guard->planModels->addItem(m["display_name"].toString(m["slug"].toString()),
                                           m["slug"].toString());
        }
        int index = guard->planModels->findData(previous);
        if (index < 0)
            index = guard->planModels->findData("gpt-6.1-sol");
        guard->planModels->setCurrentIndex(index >= 0 ? index : guard->planModels->count() ? 0 : -1);
        if (guard->planModels->count()) {
            auto c = AppSettings().config("ChatGPT");
            c.model = guard->planModels->currentData().toString();
            AppSettings().save(c);
            guard->planStatus->setText(QStringLiteral("ChatGPT 套餐已连接；模型来自当前账户可用列表"));
        } else
            guard->planStatus->setText(
                QStringLiteral("当前账户没有返回可用模型，请检查套餐授权与工作区限制"));
    });
}
bool SettingsDialog::save() {
    if (speechMode)
        AppSettings().saveLocalTranscription(speechMode->currentData().toString(),
                                             localModel->text().trimmed());
    if (planModels && planModels->currentIndex() >= 0) {
        auto c = AppSettings().config("ChatGPT");
        c.model = planModels->currentData().toString();
        AppSettings().save(c);
    }
    for (auto f : fields) {
        if (f.provider == "Transcription" && AppSettings().transcriptionMode() == "Local")
            continue;
        ProviderConfig c;
        c.provider = f.provider;
        c.baseUrl = f.url->text().trimmed();
        c.model = f.model->text().trimmed();
        c.accountLogin = f.provider == "DeepSeek" && deepSeekMode->currentData().toBool();
        if (c.accountLogin) {
            AppSettings().save(c);
            continue;
        }
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
        if (AppSettings().transcriptionMode() == "Local") {
            auto local = new LocalTranscriber(this);
            QPointer<SettingsDialog> guard(this);
            connect(local, &LocalTranscriber::status, this, [f, local, guard](QString, QString message) {
                if (!guard)
                    return;
                f.result->setText(message);
                if (message.contains(QStringLiteral("就绪")) || message.contains(QStringLiteral("失败"))) {
                    local->stop();
                    local->deleteLater();
                }
            });
            local->connectSession(newId(), "test", AppSettings().localModelPath());
            QTimer::singleShot(30000, local, [f, local, guard] {
                if (guard)
                    f.result->setText(QStringLiteral("本地识别初始化超时，请检查模型路径"));
                local->stop();
                local->deleteLater();
            });
            return;
        }
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
