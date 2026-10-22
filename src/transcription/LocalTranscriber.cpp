#include "LocalTranscriber.h"
#include "LocalWorker.h"
#include "domain/Types.h"
#include <QCoreApplication>
#include <QRegularExpression>
QJsonObject LocalResultAssembler::apply(QString sid, QString channel, QString text, bool final, qint64 start,
                                        qint64 end) {
    text = text.simplified();
    text.replace(QRegularExpression("([\\p{Han}])\\s+(?=[\\p{Han}])"), "\\1");
    if (text.isEmpty() && row.isEmpty())
        return {};
    if (row.isEmpty())
        row = {{"id", newId()}, {"sessionId", sid},        {"channel", channel},    {"startMs", start},
               {"revision", 0}, {"approximateTime", true}, {"engine", "Vosk-local"}};
    if (!final && row.value("text") == text)
        return {};
    row["text"] = text;
    row["final"] = final;
    row["endMs"] = end;
    row["revision"] = row["revision"].toInt() + 1;
    auto result = row;
    if (final)
        row = {};
    return result;
}
LocalTranscriber::LocalTranscriber(QObject *p) : QObject(p) {}
LocalTranscriber::~LocalTranscriber() {
    stop();
}
void LocalTranscriber::connectSession(QString sid, QString ch, QString model, QString library) {
    stop();
    channel = ch;
    auto epoch = generation;
    if (library.isEmpty())
        library = QCoreApplication::applicationDirPath() + "/local-asr/libvosk.dll";
    auto w = new LocalWorker(sid, ch, model, library);
    worker = w;
    connect(w, &LocalWorker::text, this, [this, epoch](QJsonObject row) {
        if (epoch == generation) {
            if (row["final"].toBool())
                finalizedEnd = qMax(finalizedEnd, row["endMs"].toInteger());
            emit segmentChanged(row);
        }
    });
    connect(w, &LocalWorker::message, this, [this, epoch, ch](QString message) {
        if (epoch == generation)
            emit status(ch, message);
    });
    connect(w, &LocalWorker::failure, this, [this, epoch, ch](QString message) {
        if (epoch != generation)
            return;
        engineFailed = true;
        if (submittedStart >= 0)
            emit gap(ch, qMax(submittedStart, finalizedEnd), submittedEnd);
        emit failed(ch, message);
    });
    connect(w, &QThread::finished, w, &QObject::deleteLater);
    w->start();
}
void LocalTranscriber::appendAudio(const QByteArray &pcm, qint64 start) {
    if (pcm.isEmpty())
        return;
    if (submittedStart < 0)
        submittedStart = start;
    submittedEnd = start + pcm.size() / 48;
    if (engineFailed) {
        emit gap(channel, start, submittedEnd);
        return;
    }
    if (!worker)
        return;
    auto lost = worker->append(pcm, start);
    if (lost.first >= 0)
        emit gap(channel, lost.first, lost.second);
}
void LocalTranscriber::finish(bool closeInput) {
    if (worker)
        worker->finish(closeInput);
}
void LocalTranscriber::stop() {
    generation++;
    engineFailed = false;
    submittedStart = -1;
    submittedEnd = finalizedEnd = 0;
    if (worker) {
        auto w = worker.data();
        worker = nullptr;
        disconnect(w, nullptr, this, nullptr);
        w->abort();
    }
}
bool LocalTranscriber::hasUnfinishedAudio() const {
    return worker && worker->pending();
}
qint64 LocalTranscriber::bufferedMilliseconds() const {
    return worker ? worker->buffered() / 48 : 0;
}
