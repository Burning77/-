#include "app/AppSettings.h"
#include <QTemporaryDir>
#include <QtTest>
class SettingsTest : public QObject {
    Q_OBJECT
  private slots:
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
    }
};
QTEST_MAIN(SettingsTest)
#include "tst_settings.moc"
