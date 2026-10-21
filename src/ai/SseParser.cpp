#include "SseParser.h"
QList<SseEvent> SseParser::feed(const QByteArray &bytes) {
    buffer += bytes;
    QList<SseEvent> result;
    while (true) {
        auto end = buffer.indexOf('\n');
        if (end < 0)
            break;
        auto raw = buffer.left(end);
        buffer.remove(0, end + 1);
        if (raw.endsWith('\r'))
            raw.chop(1);
        auto line = QString::fromUtf8(raw);
        if (line.isEmpty()) {
            if (hasData)
                result.append({type, data});
            type.clear();
            data.clear();
            hasData = false;
            continue;
        }
        if (line.startsWith(':'))
            continue;
        auto colon = line.indexOf(':');
        auto key = colon < 0 ? line : line.left(colon);
        auto value = colon < 0 ? QString() : line.mid(colon + 1);
        if (value.startsWith(' '))
            value.remove(0, 1);
        if (key == "event")
            type = value;
        if (key == "data") {
            if (hasData)
                data += '\n';
            data += value;
            hasData = true;
        }
    }
    return result;
}
