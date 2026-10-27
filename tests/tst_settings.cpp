#include "app/AppSettings.h"
#include "ai/TextClient.h"
#include "ui/SettingsDialog.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
class SettingsTest : public QObject {
    Q_OBJECT
  private slots:
    void chatGptReasoningDefaultsToHighAndPreservesModel_data() {
        QTest::addColumn<QString>("effort");
        for (const auto &value : {"missing", "high", "medium", "low", "xhigh", "max", ""})
            QTest::newRow(*value ? value : "model-default") << QString(value);
    }
    void chatGptReasoningDefaultsToHighAndPreservesModel() {
        QFETCH(QString, effort);
        QTemporaryDir dir;
        QCoreApplication::setOrganizationName("InterviewTests");
        QCoreApplication::setApplicationName("ReasoningPayload");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QSettings().setValue("ChatGPT/model", "gpt-6.1-sol");
        if (effort != "missing")
            QSettings().setValue("ChatGPT/reasoningEffort", effort);
        TextRequest request;
        request.config = AppSettings().config("ChatGPT");
        AppSettings().save(request.config);
        request.config = AppSettings().config("ChatGPT");
        const auto payload = TextClient::payload(request);
        QCOMPARE(payload["model"].toString(), QString("gpt-6.1-sol"));
        if (effort.isEmpty())
            QVERIFY(!payload.contains("reasoning"));
        else
            QCOMPARE(payload["reasoning"].toObject()["effort"].toString(),
                     effort == "missing" ? "high" : effort);
    }
    void chatGptReasoningCanBeSavedWithoutModelList() {
        QTemporaryDir dir;
        QCoreApplication::setOrganizationName("InterviewTests");
        QCoreApplication::setApplicationName("ReasoningSettings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QStandardPaths::setTestModeEnabled(true);
        auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
        for (const auto &name : {"OpenAI/url", "DeepSeek/url", "transcription/url"})
            QSettings().setValue(name, "https://" + newId() + ".invalid");
        SettingsDialog dialog;
        auto effort = dialog.findChild<QComboBox *>("chatgptReasoningEffort");
        QVERIFY(effort);
        QCOMPARE(effort->currentData().toString(), QString("high"));
        effort->setCurrentIndex(effort->findData("medium"));
        auto buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        buttons->button(QDialogButtonBox::Save)->click();
        QCOMPARE(QSettings().value("ChatGPT/reasoningEffort").toString(), QString("medium"));
        SettingsDialog reopened;
        QCOMPARE(reopened.findChild<QComboBox *>("chatgptReasoningEffort")->currentData().toString(),
                 QString("medium"));
    }
    void localDefaultUpgradesChineseOnlyModelButKeepsNewCustomPath() {
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName("InterviewTests");
        QCoreApplication::setApplicationName("AsrMigration");
        QSettings().setValue("transcription/localModel", "old-package/local-asr/model");
        AppSettings a;
        QVERIFY(a.localModelPath().endsWith("/local-asr/bilingual"));
        a.saveLocalTranscription("Local", "custom-bilingual-model");
        QCOMPARE(a.localModelPath(), QString("custom-bilingual-model"));
        QCOMPARE(QSettings().value("transcription/localModel").toString(),
                 QString("old-package/local-asr/model"));
    }
    void keepsProviderConfigurationSeparate() {
        QTemporaryDir dir(QDir::currentPath() + "/settings-XXXXXX");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName("InterviewTests");
        QCoreApplication::setApplicationName("Settings");
        AppSettings a;
        ProviderConfig o;
        o.model = "open-model";
        a.save(o);
        ProviderConfig d;
        d.provider = "DeepSeek";
        d.baseUrl = "https://api.deepseek.com";
        d.model = "deep-model";
        a.save(d);
        a.select("DeepSeek");
        QCOMPARE(a.textConfig().model, QString("deep-model"));
        QCOMPARE(a.config("OpenAI").model, QString("open-model"));
        QVERIFY(o.credentialId() != d.credentialId());
        d.baseUrl = "https://different.example";
        QVERIFY(d.credentialId() != a.textConfig().credentialId());
        a.save(d);
        d.accountLogin = true;
        d.baseUrl = "https://api.deepseek.com";
        a.save(d);
        QVERIFY(a.config("DeepSeek").accountLogin);
        QCOMPARE(a.config("DeepSeek").baseUrl, QString("https://api.deepseek.com"));
        QCOMPARE(QSettings().value("DeepSeek/url").toString(), QString("https://different.example"));
        d.accountLogin = false;
        d.baseUrl = "https://different.example";
        a.save(d);
        QVERIFY(!a.config("DeepSeek").accountLogin);
        QCOMPARE(a.config("DeepSeek").baseUrl, QString("https://different.example"));
    }
};
QTEST_MAIN(SettingsTest)
#include "tst_settings.moc"
