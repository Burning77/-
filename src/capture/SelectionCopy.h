#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <functional>
struct SelectionTextResult {
    QString text;
    QString error;
    quint64 revision = 0;
    bool retryable = false;
};
using SelectionTextCallback = std::function<void(SelectionTextResult)>;
struct SelectionBackend {
    std::function<quintptr()> foreground;
    std::function<bool(quintptr)> external;
    std::function<bool()> released;
    std::function<quint64()> clipboardRevision;
    std::function<bool(quintptr)> clipboardMatchesTarget;
    std::function<bool()> copy;
    std::function<void(quint64, SelectionTextCallback)> readText;
};
class SelectionCopy : public QObject {
    Q_OBJECT
  public:
    explicit SelectionCopy(QObject *parent = nullptr, SelectionBackend backend = {});
    ~SelectionCopy() override;
    void start();
    void cancel();
  signals:
    void finished(QString text, QString error);

  private:
    SelectionBackend backend;
    QTimer poll;
    QElapsedTimer elapsed;
    QPointer<QProcess> reader;
    quintptr target = 0;
    quint64 baseline = 0, serial = 0;
    bool copied = false, reading = false;
    void step();
    void finish(QString text, QString error);
    void readClipboard(quint64 revision, SelectionTextCallback callback);
};
