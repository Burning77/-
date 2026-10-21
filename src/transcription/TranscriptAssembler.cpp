#include "TranscriptAssembler.h"
#include "domain/Types.h"
QJsonObject TranscriptAssembler::apply(const QJsonObject &e, const QString &sid, const QString &channel,
                                       qint64 start, qint64 end) {
    auto type = e["type"].toString();
    auto item = e["item_id"].toString();
    bool final = type == "conversation.item.input_audio_transcription.completed";
    if (item.isEmpty() || (!final && type != "conversation.item.input_audio_transcription.delta"))
        return {};
    auto key = sid + "/" + channel + "/" + item;
    auto row = items.value(key);
    if (row.value("final").toBool() && (!final || row.value("text") == e["transcript"]))
        return {};
    if (row.isEmpty())
        row = {{"id", newId()},    {"sessionId", sid}, {"channel", channel}, {"itemId", item},
               {"startMs", start}, {"revision", 0},    {"text", ""}};
    row["text"] = final ? e["transcript"].toString() : row["text"].toString() + e["delta"].toString();
    row["final"] = final;
    row["endMs"] = end;
    row["revision"] = row["revision"].toInt() + 1;
    row["approximateTime"] = true;
    items[key] = row;
    return row;
}
