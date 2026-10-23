#include "app/AppSettings.h"
#include <QTemporaryDir>
#include <QtTest>
class SettingsTest : public QObject {
    Q_OBJECT
  private slots:
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
