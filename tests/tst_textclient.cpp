#include "ai/TextClient.h"
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
class TextClientTest : public QObject {
    Q_OBJECT
  private slots:
    void chatGptPlanPayloadUsesInstructionsAndNoStore() {
        TextRequest r;
        r.config.provider = "ChatGPT";
        r.config.model = "account-model";
        r.messages = {QJsonObject{{"role", "system"}, {"content", "coach"}},
                      QJsonObject{{"role", "user"}, {"content", "question"}}};
        auto p = TextClient::payload(r);
        QCOMPARE(p["store"].toBool(true), false);
        QCOMPARE(p["instructions"].toString(), QString("coach"));
        QCOMPARE(p["input"].toArray().size(), 1);
        QCOMPARE(p["input"].toArray()[0].toObject()["role"].toString(), QString("user"));
    }
    void chatGptNeverSendsCredentialToCustomEndpoint() {
        TextClient c;
        int reads = 0;
        c.keyReader = [&](const QString &) {
            reads++;
            return QString("fixture");
        };
        QSignalSpy errors(&c, &TextClient::failed);
        TextRequest r;
        r.config.provider = "ChatGPT";
        r.config.model = "fixture";
        r.config.baseUrl = "https://example.invalid/v1";
        c.start(r);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 1000);
        QVERIFY(errors.first()[1].toString().contains(QStringLiteral("官方")));
        QCOMPARE(reads, 0);
    }
    void preservesSafeServiceErrorDetails_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("success");
        QTest::newRow("http-auth")
            << 401
            << QByteArray(
                   R"({"error":{"code":"invalid_api_key","message":"Incorrect key fixture-secret-key and Bearer another-secret-token"}})")
            << QString("invalid_api_key") << false;
        QTest::newRow("http-model")
            << 404
            << QByteArray(
                   R"({"error":{"code":"model_not_found","message":"The requested model is unavailable."}})")
            << QString("model_not_found") << false;
        QTest::newRow("http-quota")
            << 429
            << QByteArray(
                   R"({"error":{"type":"insufficient_quota","code":"credit_balance_exhausted","message":"Add credits to continue."}})")
            << QString("credit_balance_exhausted") << false;
        QTest::newRow("http-html") << 502 << QByteArray("<html>private proxy diagnostics</html>")
                                   << QString("HTTP 502") << false;
        QTest::newRow("stream-nested-error")
            << 200
            << QByteArray("data: {\"error\":{\"code\":\"permission_denied\",\"message\":\"This project "
                          "cannot use the model.\"}}\n\n")
            << QString("permission_denied") << false;
        QTest::newRow("stream-official-error")
            << 200
            << QByteArray("event: error\ndata: "
                          "{\"type\":\"error\",\"code\":\"server_error\",\"message\":\"Retry later.\"}\n\n")
            << QString("server_error") << false;
        QTest::newRow("stream-response-failed")
            << 200
            << QByteArray("data: "
                          "{\"type\":\"response.failed\",\"response\":{\"error\":{\"code\":\"server_error\","
                          "\"message\":\"Generation failed.\"}}}\n\n")
            << QString("Generation failed.") << false;
        QTest::newRow("stream-incomplete")
            << 200
            << QByteArray("data: "
                          "{\"type\":\"response.incomplete\",\"response\":{\"incomplete_details\":{"
                          "\"reason\":\"max_output_tokens\"}}}\n\n")
            << QString("max_output_tokens") << false;
        QTest::newRow("stream-null-error-is-success")
            << 200
            << QByteArray(
                   "data: {\"type\":\"response.output_text.delta\",\"error\":null,\"delta\":\"OK\"}\n\ndata: "
                   "{\"type\":\"response.completed\",\"response\":{\"error\":null}}\n\n")
            << QString("OK") << true;
    }
    void preservesSafeServiceErrorDetails() {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, expected);
        QFETCH(bool, success);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [=] {
                socket->readAll();
                if (socket->property("sent").toBool())
                    return;
                socket->setProperty("sent", true);
                auto contentType = status == 200 ? "text/event-stream" : "application/json";
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Type: " +
                              contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                              "\r\nConnection: close\r\n\r\n");
                socket->write(body.left(body.size() / 2));
                QTimer::singleShot(10, socket, [=] {
                    socket->write(body.mid(body.size() / 2));
                    socket->disconnectFromHost();
                });
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        TextClient client;
        client.keyReader = [](const QString &) { return QString("fixture-secret-key"); };
        QSignalSpy errors(&client, &TextClient::failed), done(&client, &TextClient::finished),
            delta(&client, &TextClient::textDelta);
        TextRequest request;
        request.config.model = "fixture-model";
        request.config.baseUrl = QString("http://127.0.0.1:%1").arg(server.serverPort());
        client.start(request);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty() || !done.isEmpty(), 3000);
        if (success) {
            QCOMPARE(done.size(), 1);
            QVERIFY(errors.isEmpty());
            QCOMPARE(delta.first()[1].toString(), expected);
        } else {
            QVERIFY(done.isEmpty());
            QCOMPARE(errors.size(), 1);
            auto error = errors.first()[1].toString();
            QVERIFY2(error.contains(expected), qPrintable(error));
            QVERIFY(!error.contains("fixture-secret-key"));
            QVERIFY(!error.contains("another-secret-token"));
            QVERIFY(!error.contains("private proxy diagnostics"));
            if (status != 200)
                QVERIFY(error.contains(QString("HTTP %1").arg(status)));
        }
    }
    void usesDifferentWireFormats() {
        TextRequest r;
        r.config.model = "chosen-model";
        r.messages = QJsonArray{QJsonObject{{"role", "user"}, {"content", "hello"}}};
        auto o = TextClient::payload(r);
        QVERIFY(o.contains("input"));
        QVERIFY(!o.contains("messages"));
        r.config.provider = "DeepSeek";
        auto d = TextClient::payload(r);
        QVERIFY(d.contains("messages"));
        QVERIFY(!d.contains("input"));
        QCOMPARE(d["model"].toString(), QString("chosen-model"));
    }
    void realHttpStreamingAndTruncation() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto s = server.nextPendingConnection();
            connect(s, &QTcpSocket::readyRead, s, [s] {
                if (s->property("sent").toBool())
                    return;
                s->setProperty("sent", true);
                s->readAll();
                s->write(
                    "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\ndata: "
                    "{\"choices\":[{\"delta\":{\"content\":\"answer\"}}]}\n\ndata: [DONE]\n\n");
                s->disconnectFromHost();
            });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        });
        TextClient c;
        c.keyReader = [](const QString &) { return QString("test-only-key"); };
        QSignalSpy delta(&c, &TextClient::textDelta), done(&c, &TextClient::finished),
            error(&c, &TextClient::failed);
        TextRequest r;
        r.config.provider = "DeepSeek";
        r.config.model = "test";
        r.config.baseUrl = QString("http://127.0.0.1:%1").arg(server.serverPort());
        c.start(r);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty() || !error.isEmpty(), 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error.isEmpty() ? QString() : error.first()[1].toString()));
        QCOMPARE(done.size(), 1);
        QCOMPARE(delta.size(), 1);
        QCOMPARE(delta.first()[1].toString(), QString("answer"));
    }
};
QTEST_MAIN(TextClientTest)
#include "tst_textclient.moc"
