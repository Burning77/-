#include "transcription/WebSocketTransport.h"
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
class WebSocketTest : public QObject {
    Q_OBJECT
  private slots:
    void upgradesAndReceivesNativeWebSocket() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                auto input = socket->property("input").toByteArray() + socket->readAll();
                socket->setProperty("input", input);
                if (socket->property("upgraded").toBool() || !input.contains("\r\n\r\n"))
                    return;
                QByteArray key;
                for (auto line : input.split('\n'))
                    if (line.toLower().startsWith("sec-websocket-key:"))
                        key = line.mid(line.indexOf(':') + 1).trimmed();
                auto accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
                                                       QCryptographicHash::Sha1)
                                  .toBase64();
                socket->setProperty("upgraded", true);
                socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: "
                              "Upgrade\r\nSec-WebSocket-Accept: " +
                              accept + "\r\n\r\n");
                socket->write(QByteArray::fromHex("8105") + "hello");
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        WebSocketTransport transport(QUrl(QString("ws://127.0.0.1:%1/live").arg(server.serverPort())),
                                     "test-only-key");
        QSignalSpy message(&transport, &WebSocketTransport::message),
            errors(&transport, &WebSocketTransport::transportError);
        transport.start();
        QTRY_COMPARE_WITH_TIMEOUT(message.size(), 1, 5000);
        QCOMPARE(message[0][0].toByteArray(), QByteArray("hello"));
        QCOMPARE(errors.size(), 0);
        transport.stop();
        QVERIFY(transport.wait(2000));
    }
};
QTEST_MAIN(WebSocketTest)
#include "tst_websocket.moc"
