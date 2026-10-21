#pragma once
#include <QHash>
#include <QQueue>
#include <QSet>
class TurnTracker {
  public:
    using Range = QPair<qint64, qint64>;
    void queuedCommit(qint64 start, qint64 end) {
        commits.enqueue({start, end});
    }
    void acknowledge(const QString &id) {
        if (id.isEmpty() || ranges.contains(id) || commits.isEmpty())
            return;
        ranges[id] = commits.dequeue();
        if (!completedItems.contains(id))
            unfinished.insert(id);
    }
    void completed(const QString &id) {
        unfinished.remove(id);
        completedItems.insert(id);
    }
    Range rangeFor(const QString &id, Range fallback) const {
        return ranges.value(id, fallback);
    }
    QList<Range> retireConnection() {
        QList<Range> lost;
        while (!commits.isEmpty())
            lost.append(commits.dequeue());
        for (auto id : unfinished)
            lost.append(ranges.value(id));
        ranges.clear();
        unfinished.clear();
        completedItems.clear();
        return lost;
    }
    bool pending() const {
        return !commits.isEmpty() || !unfinished.isEmpty();
    }

  private:
    QQueue<Range> commits;
    QHash<QString, Range> ranges;
    QSet<QString> unfinished, completedItems;
};
