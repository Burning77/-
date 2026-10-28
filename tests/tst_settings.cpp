#include "app/AppSettings.h"
#include "app/LocalModelPath.h"
#include "ai/TextClient.h"
#include "ui/SettingsDialog.h"
#include "ui/PlaybackDeviceSelector.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
class SettingsTest : public QObject {
    Q_OBJECT
    static bool createModel(const QString &path, const QString &missing = {}) {
        if (!QDir().mkpath(path))
            return false;
        for (const auto &name : {"encoder-epoch-99-avg-1.int8.onnx", "decoder-epoch-99-avg-1.onnx",
                                 "joiner-epoch-99-avg-1.int8.onnx", "tokens.txt"}) {
            if (name == missing)
                continue;
            QFile file(path + "/" + name);
            if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7)
                return false;
        }
        return true;
    }
  private slots:
    void playbackSelectionUsesSystemDefaultOrSavedDevice_data() {
        QTest::addColumn<QString>("preferred");
        QTest::addColumn<QString>("expected");
        QTest::newRow("first-virtual-device-is-not-default") << QString() << QString();
        QTest::newRow("remember-realtek") << QString("realtek") << QString("realtek");
        QTest::newRow("explicit-virtual-device") << QString("steam") << QString("steam");
        QTest::newRow("other-computer-or-unplugged-device") << QString("missing") << QString();
    }
    void playbackSelectionUsesSystemDefaultOrSavedDevice() {
        QFETCH(QString, preferred);
        QFETCH(QString, expected);
        QComboBox combo;
        populatePlaybackDevices(combo, {{"steam", "Steam Streaming Speakers"}, {"realtek", "Realtek"}},
                                preferred);
        QCOMPARE(combo.currentData().toString(), expected);
        QCOMPARE(combo.count(), 3);
        QCOMPARE(combo.itemData(0).toString(), QString());
    }
    void playbackDeviceRefreshKeepsIdentityAcrossEnumerationOrder() {
        QComboBox combo;
        populatePlaybackDevices(combo, {{"steam", "Steam"}, {"realtek", "Realtek"}}, "realtek");
        QCOMPARE(combo.currentData().toString(), QString("realtek"));
        populatePlaybackDevices(combo, {{"realtek", "Realtek"}, {"steam", "Steam"}}, "realtek");
        QCOMPARE(combo.currentData().toString(), QString("realtek"));
        populatePlaybackDevices(combo, {}, "realtek");
        QCOMPARE(combo.currentData().toString(), QString());
        QCOMPARE(combo.count(), 1);
    }
    void emptyLocalModelSettingUsesCurrentBundle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCoreApplication::setOrganizationName("InterviewTests");
        QCoreApplication::setApplicationName("EmptyAsrPath");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        const auto bundled = QCoreApplication::applicationDirPath() + "/local-asr/bilingual";
        for (const auto &value : {QString(), QString("   ")}) {
            AppSettings().saveLocalTranscription("Local", value);
            QCOMPARE(AppSettings().localModelPath(), bundled);
            QCOMPARE(QSettings().value("transcription/bilingualModel").toString(), value);
        }
    }
    void versionedLocalModelPathMovesToCurrentBundle_data() {
        QTest::addColumn<bool>("oldModelPresent");
        QTest::newRow("old-release-deleted") << false;
        QTest::newRow("old-release-still-present") << true;
    }
    void versionedLocalModelPathMovesToCurrentBundle() {
        QFETCH(bool, oldModelPresent);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto appDir = dir.path() + "/InterviewAssistant-0.7.1";
        const auto bundled = appDir + "/local-asr/bilingual";
        const auto old = dir.path() + "/InterviewAssistant-0.5.0/local-asr/bilingual";
        QVERIFY(createModel(bundled));
        if (oldModelPresent)
            QVERIFY(createModel(old));
        QCOMPARE(resolveLocalModelPath(old, appDir), bundled);
        auto windowsPath = old;
        windowsPath.replace('/', '\\');
        QCOMPARE(resolveLocalModelPath(windowsPath + "\\", appDir), bundled);
    }
    void customLocalModelPathsArePreserved_data() {
        QTest::addColumn<bool>("modelPresent");
        QTest::newRow("custom-model-present") << true;
        QTest::newRow("custom-model-missing") << false;
    }
    void customLocalModelPathsArePreserved() {
        QFETCH(bool, modelPresent);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto appDir = dir.path() + "/InterviewAssistant-0.7.1";
        const auto custom = dir.path() + "/custom-models/local-asr/bilingual";
        QVERIFY(createModel(appDir + "/local-asr/bilingual"));
        if (modelPresent)
            QVERIFY(createModel(custom));
        QCOMPARE(resolveLocalModelPath(custom, appDir), custom);
    }
    void incompleteCurrentBundleDoesNotReplaceConfiguredPath_data() {
        QTest::addColumn<QString>("missing");
        QTest::newRow("encoder") << QString("encoder-epoch-99-avg-1.int8.onnx");
        QTest::newRow("decoder") << QString("decoder-epoch-99-avg-1.onnx");
        QTest::newRow("joiner") << QString("joiner-epoch-99-avg-1.int8.onnx");
        QTest::newRow("tokens") << QString("tokens.txt");
        QTest::newRow("empty-file") << QString();
    }
    void incompleteCurrentBundleDoesNotReplaceConfiguredPath() {
        QFETCH(QString, missing);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto appDir = dir.path() + "/InterviewAssistant-0.7.1";
        const auto bundled = appDir + "/local-asr/bilingual";
        const auto old = dir.path() + "/InterviewAssistant-0.5.0/local-asr/bilingual";
        QVERIFY(createModel(bundled, missing));
        if (missing.isEmpty()) {
            QFile tokens(bundled + "/tokens.txt");
            QVERIFY(tokens.open(QIODevice::WriteOnly | QIODevice::Truncate));
        }
        QCOMPARE(resolveLocalModelPath(old, appDir), old);
    }
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
