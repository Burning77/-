#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>
#include <functional>
#include <memory>
class DeepSeekAccount : public QObject {
    Q_OBJECT
  public:
    using JsonCallback = std::function<void(QJsonObject, QString)>;
    using TokenCallback = std::function<void(QString, QString)>;
    explicit DeepSeekAccount(QObject *parent = nullptr, QString vault = {});
    static DeepSeekAccount &shared();
    bool signedIn() const;
    QString sessionId() const;
    void login();
    void cancelLogin();
    void signOut();
    void token(QString session, TokenCallback done);
    void models(JsonCallback done);
    void rejectToken(QString session);
    static QList<QPair<QByteArray, QByteArray>> headers();
    std::function<bool(QUrl)> openBrowser;
    std::function<void(QString, QUrl, QJsonObject, QString, JsonCallback)> transport;
  signals:
    void changed();
    void status(QString);
    void invalidated(QString session);

  private:
    struct State;
    std::unique_ptr<State> state;

  public:
    ~DeepSeekAccount();
};
