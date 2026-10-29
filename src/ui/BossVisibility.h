#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWidget>

// Hide native windows without ending a QDialog::exec() event loop.
class BossVisibility : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit BossVisibility(QWidget *root);
    ~BossVisibility() override;
    void toggle();
    bool hidden() const {
        return hiding;
    }
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;

  signals:
    void restored();

  private:
    QPointer<QWidget> root;
    QPointer<QWidget> active;
    QList<QPointer<QWidget>> windows;
    bool hiding = false;
    bool owns(QWidget *) const;
};
