#include "ai/SseParser.h"
#include <QtTest>
class SseTest : public QObject {
    Q_OBJECT
  private slots:
    void preservesSplitChineseAndCrLf() {
        SseParser p;
        QList<SseEvent> events;
        QByteArray bytes = QStringLiteral("event: delta\r\ndata: 你好\r\n\r\ndata: [DONE]\n\n").toUtf8();
        for (char b : bytes)
            events += p.feed(QByteArray(1, b));
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].data, QStringLiteral("你好"));
        QCOMPARE(events[0].event, QString("delta"));
        QCOMPARE(events[1].data, QString("[DONE]"));
    }
    void joinsDataLinesAndIgnoresComments() {
        SseParser p;
        auto e = p.feed(": ping\ndata: one\ndata: two\n\n");
        QCOMPARE(e.size(), 1);
        QCOMPARE(e[0].data, QString("one\ntwo"));
    }
};
QTEST_MAIN(SseTest)
#include "tst_sse.moc"
