#include "preparation/Preparation.h"
#include "ai/InterviewLogic.h"
#include "ui/PreparationDialog.h"
#include "app/AppSettings.h"
#include <QLineEdit>
#include <QPushButton>
#include <QTextBrowser>
#include <QTableWidget>
#include <QTabWidget>
#include <QFontDatabase>
#include <cstring>
#include <QTemporaryDir>
#include <QtTest>
class PreparationReply : public QNetworkReply {
    QByteArray bytes;
    qsizetype offset = 0;
    bool ended = false;

  public:
    PreparationReply(QByteArray response, QObject *parent) : QNetworkReply(parent), bytes(response) {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(30, this, [this] {
            if (ended)
                return;
            emit readyRead();
            ended = true;
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {
        if (ended)
            return;
        ended = true;
        setError(OperationCanceledError, "cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return bytes.size() - offset + QNetworkReply::bytesAvailable();
    }
    qint64 readData(char *out, qint64 count) override {
        auto n = qMin(count, qint64(bytes.size() - offset));
        if (!n)
            return -1;
        std::memcpy(out, bytes.constData() + offset, n);
        offset += n;
        return n;
    }
};
class PreparationTest : public QObject {
    Q_OBJECT
    QTemporaryDir settings;
    QJsonObject prepared() {
        QJsonObject p{{"company", QStringLiteral("示例科技")},
                      {"role", QStringLiteral("嵌入式工程师")},
                      {"jobDescription", "C++ Qt RTOS"},
                      {"analysis", QStringLiteral("重点准备线程与事件循环")},
                      {"terms", QJsonArray{QJsonObject{{"term", "Qt"},
                                                       {"aliases", QJsonArray{"Q T", QStringLiteral("丘特")}},
                                                       {"meaning", QStringLiteral("跨平台界面框架")}},
                                           QJsonObject{{"term", "RTOS"},
                                                       {"aliases", QJsonArray{"R T O S"}},
                                                       {"meaning", QStringLiteral("实时操作系统")}}}}};
        p["analysisBasis"] = Preparation::fingerprint(p);
        p["termsBasis"] = Preparation::fingerprint(p);
        return p;
    }
  private slots:
    void initTestCase() {
        QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
        QApplication::setFont(QFont("Microsoft YaHei", 9));
        QCoreApplication::setOrganizationName("InterviewAssistantTests");
        QCoreApplication::setApplicationName("PreparationRegression");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        ProviderConfig config;
        config.model = "fixture-model";
        AppSettings().save(config);
        AppSettings().select("OpenAI");
    }
    void dialogAnalysisCompletesAndCancellationKeepsOnlyDraft() {
        QTemporaryDir dir;
        auto p = prepared();
        p.remove("analysis");
        p.remove("analysisBasis");
        auto result = QJsonObject{{"analysis", QStringLiteral("测试分析结果")}, {"terms", p["terms"]}};
        auto event =
            QJsonObject{{"type", "response.output_text.delta"},
                        {"delta", QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))}};
        QByteArray body = "data: " + QJsonDocument(event).toJson(QJsonDocument::Compact) +
                          "\n\ndata: {\"type\":\"response.completed\"}\n\n";
        PreparationDialog dialog(dir.path(), p);
        auto client = dialog.findChild<TextClient *>();
        QVERIFY(client);
        client->keyReader = [](QString) { return QString("fixture"); };
        client->postRequest = [&](const QNetworkRequest &, const QByteArray &) {
            return static_cast<QNetworkReply *>(new PreparationReply(body, client));
        };
        dialog.findChild<QPushButton *>("analyzePreparation")->click();
        QVERIFY(!dialog.findChild<QLineEdit *>("prepCompany")->isEnabled());
        QTRY_COMPARE(Preparation::load(dir.path())["analysis"].toString(), QStringLiteral("测试分析结果"));
        QVERIFY(dialog.findChild<QLineEdit *>("prepCompany")->isEnabled());
        QCOMPARE(dialog.findChild<QTableWidget *>("prepTerms")->rowCount(), 2);
        dialog.findChild<QTabWidget *>()->setCurrentIndex(2);
        QVERIFY(dialog.grab().save(QDir::currentPath() + "/preparation-terms-preview.png"));
        QTemporaryDir cancelledDir;
        PreparationDialog cancelled(cancelledDir.path(), p);
        auto pending = cancelled.findChild<TextClient *>();
        pending->keyReader = [](QString) { return QString("fixture"); };
        pending->postRequest = [&](const QNetworkRequest &, const QByteArray &) {
            return static_cast<QNetworkReply *>(new PreparationReply(body, pending));
        };
        cancelled.findChild<QPushButton *>("analyzePreparation")->click();
        cancelled.reject();
        QTest::qWait(70);
        auto draft = Preparation::load(cancelledDir.path());
        QVERIFY(draft["analysis"].toString().isEmpty());
        QCOMPARE(draft["company"], p["company"]);
    }
    void savingChangedRoleDoesNotApplyOldAnalysisOrUntouchedGlossary() {
        QTemporaryDir dir;
        auto p = prepared();
        PreparationDialog dialog(dir.path(), p);
        dialog.findChild<QLineEdit *>("prepRole")->setText(QStringLiteral("财务"));
        dialog.findChild<QPushButton *>("savePreparation")->click();
        auto saved = Preparation::load(dir.path());
        QVERIFY(!Preparation::context(saved).contains(QStringLiteral("重点准备线程")));
        QJsonObject row{{"text", QStringLiteral("丘特")}, {"final", true}};
        QCOMPARE(Preparation::correct(row, saved), row);
    }

    void savesDraftAndBuildsAnalysisUsingOnlyProvidedInputs() {
        QTemporaryDir dir;
        auto p = prepared();
        QString error;
        QVERIFY(Preparation::save(dir.path(), p, &error));
        QCOMPARE(Preparation::load(dir.path()), p);
        ProviderConfig config;
        config.provider = "DeepSeek";
        auto r = Preparation::request(p, config);
        QVERIFY(r.jsonMode);
        QCOMPARE(r.config.provider, QString("DeepSeek"));
        auto text = QString::fromUtf8(QJsonDocument(r.messages).toJson());
        QVERIFY(text.contains(QStringLiteral("示例科技")));
        QVERIFY(text.contains("RTOS"));
        QVERIFY(text.contains(QStringLiteral("未联网")));
        auto result = Preparation::parseResult(
            R"({"analysis":"岗位重点","terms":[{"term":"Qt","aliases":["Q T"],"meaning":"框架"}]})", &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result["terms"].toArray().size(), 1);
        result = Preparation::parseResult("{", &error);
        QVERIFY(result.isEmpty());
        QVERIFY(!error.isEmpty());
    }
    void finalTranscriptCorrectionsKeepRawAndAvoidPartialWordsOrAmbiguity() {
        auto p = prepared();
        QJsonObject row{{"text", QStringLiteral("请说说丘特和 R T O S")}, {"final", true}};
        auto fixed = Preparation::correct(row, p);
        QCOMPARE(fixed["text"].toString(), QStringLiteral("请说说Qt和 RTOS"));
        QCOMPARE(fixed["rawText"], row["text"]);
        row["final"] = false;
        QCOMPARE(Preparation::correct(row, p), row);
        row["final"] = true;
        row["text"] = "Q Terminal";
        QCOMPARE(Preparation::correct(row, p), row);
        auto terms = p["terms"].toArray();
        terms.append(QJsonObject{{"term", "Other"}, {"aliases", QJsonArray{QStringLiteral("丘特")}}});
        p["terms"] = terms;
        row["text"] = QStringLiteral("丘特");
        QCOMPARE(Preparation::correct(row, p), row);
    }
    void staleAnalysisAndGlossaryAreNotAppliedAfterRoleChanges() {
        auto p = prepared();
        p["role"] = QStringLiteral("财务");
        auto text = Preparation::context(p);
        QVERIFY(text.contains(QStringLiteral("财务")));
        QVERIFY(!text.contains(QStringLiteral("重点准备线程")));
        QJsonObject row{{"text", QStringLiteral("丘特")}, {"final", true}};
        QCOMPARE(Preparation::correct(row, p), row);
        QVERIFY(!Preparation::transcriptionOptions("gpt-live-transcribe", p)["keywords"].toArray().contains(
            "Qt"));
    }
    void cloudHintsAreBoundedAndDoNotSendInvalidKeywords() {
        auto p = prepared();
        auto terms = p["terms"].toArray();
        terms.append(QJsonObject{{"term", "vector<T>"}});
        terms.append(QJsonObject{{"term", "bad\nword"}});
        p["terms"] = terms;
        auto options = Preparation::transcriptionOptions("gpt-live-transcribe", p);
        QVERIFY(options["keywords"].toArray().contains("Qt"));
        QVERIFY(!options["keywords"].toArray().contains("vector<T>"));
        QVERIFY(!options.contains("language"));
        QVERIFY(options["languages"].toArray().contains("zh"));
        QVERIFY(options["prompt"].toString().size() <= 100);
    }
};
QTEST_MAIN(PreparationTest)
#include "tst_preparation.moc"
