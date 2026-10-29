#pragma once
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QHash>
#include <QObject>
#include <functional>

struct HotkeyResult {
    bool ok = false;
    QString error;
};
using HotkeyBackend = std::function<HotkeyResult(int id, quint32 modifiers, quint32 key, bool enable)>;

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalHotkey(QObject *parent = nullptr, HotkeyBackend backend = {});
    ~GlobalHotkey() override;
    static QKeySequence defaultSequence();
    static HotkeyResult validate(const QKeySequence &sequence);
    HotkeyResult setSequence(const QKeySequence &sequence);
    HotkeyResult clear();
    QKeySequence sequence() const {
        return current;
    }
    bool registered() const {
        return activeId != 0;
    }
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;

  signals:
    void activated();
    void changed();

  private:
    HotkeyBackend invoke;
    int firstId = 0;
    int activeId = 0;
    quint32 currentModifiers = 0, currentKey = 0;
    QKeySequence current;
    QHash<int, QPair<quint32, quint32>> pendingRemoval;
};
