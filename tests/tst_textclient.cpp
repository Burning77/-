#include "ai/TextClient.h"
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
class TextClientTest : public QObject {
    Q_OBJECT
  private slots:
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
