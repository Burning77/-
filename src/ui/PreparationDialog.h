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
class QCheckBox;
class ResumeUploadWidget;
class PreparationDialog : public QDialog {
    Q_OBJECT
  public:
    PreparationDialog(QString root, QJsonObject initial, bool readOnly = false, QWidget *parent = nullptr);

  protected:
    void done(int) override;

  private:
    QString root, activeId, buffer, analysisError, displayedAnalysis;
    QJsonObject data, requestInput;
    QJsonArray researchSources;
    ProviderConfig activeConfig;
    TextClient client;
    bool readOnly;
    bool glossaryEdited = false;
    bool researching = false;
    bool generatedUnsaved = false;
    QLineEdit *company, *role;
    QPlainTextEdit *job;
    ResumeUploadWidget *resume;
    QTextBrowser *analysis;
    QTableWidget *terms;
    QLabel *status;
    QLabel *analysisNotice;
    QCheckBox *onlineCompany;
    QPushButton *analyze, *stop, *saveButton, *addTerm, *removeTerm;
    QTabWidget *tabs;
    QJsonObject gather() const;
    void showTerms();
    void setBusy(bool);
    void cancelAnalysis();
    bool saveDraft();
    void runAnalysis();
    void beginPreparation();
    void failAnalysis(const QString &error, bool generated = false);
    void refreshAnalysisView();
    QJsonObject analysisResearchSnapshot() const;
};
