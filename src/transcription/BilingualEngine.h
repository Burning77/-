#pragma once
#include <QByteArray>
#include <QString>
#include <functional>
#include <memory>

// One decoding stream per audio channel; the CPU model is shared by the streams.
class BilingualEngine {
  public:
    BilingualEngine();
    ~BilingualEngine();
    bool open(const QString &modelPath, const QString &libraryPath, QString &error);
    bool accept(const QByteArray &pcm16, const std::function<bool()> &cancelled);
    QString result(bool final, const std::function<bool()> &cancelled);

  private:
    struct State;
    std::unique_ptr<State> state;
};
