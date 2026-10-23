#pragma once
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <QWidget>
class QLabel;
class QPushButton;
class ResumeUploadWidget : public QWidget {
    Q_OBJECT
  public:
    explicit ResumeUploadWidget(QWidget *parent = nullptr);
    ~ResumeUploadWidget() override;
    QJsonObject document() const {
        return data;
    }
    void setDocument(QJsonObject value, const QString &legacyExperience = {});
    void setReadOnly(bool value);
    bool busy() const {
        return importing;
    }
  public slots:
    void importFile(const QString &path);
  signals:
    void documentChanged();
    void busyChanged(bool busy);

  private:
    QJsonObject data;
    QProcess reader;
    QTimer timeout;
    QByteArray output;
    bool importing = false, readOnly = false;
    QString importError;
    QLabel *summary, *status;
    QPushButton *upload, *preview, *remove;
    void updateControls();
    void setBusy(bool value);
    void showPreview();
};
