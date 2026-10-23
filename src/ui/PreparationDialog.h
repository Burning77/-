#pragma once
#include "ai/TextClient.h"
#include <QDialog>
#include <QJsonObject>
class QLineEdit;
class QPlainTextEdit;
class QTextBrowser;
class QTableWidget;
class QLabel;
class QPushButton;
class QTabWidget;
class ResumeUploadWidget;
class PreparationDialog : public QDialog {
    Q_OBJECT
  public:
    PreparationDialog(QString root, QJsonObject initial, bool readOnly = false, QWidget *parent = nullptr);

  protected:
    void done(int) override;

  private:
    QString root, activeId, buffer;
    QJsonObject data, requestInput;
    TextClient client;
    bool readOnly;
    bool glossaryEdited = false;
    QLineEdit *company, *role;
    QPlainTextEdit *job;
    ResumeUploadWidget *resume;
    QTextBrowser *analysis;
    QTableWidget *terms;
    QLabel *status;
    QPushButton *analyze, *stop, *saveButton, *addTerm, *removeTerm;
    QTabWidget *tabs;
    QJsonObject gather() const;
    void showTerms();
    void setBusy(bool);
    void cancelAnalysis();
    bool saveDraft();
    void runAnalysis();
};
