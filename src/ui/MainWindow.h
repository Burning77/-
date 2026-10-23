#pragma once
#include "app/SessionController.h"
#include <QAudioOutput>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMediaPlayer>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
class ResumeUploadWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr, QString dataRoot = {});

  protected:
    void closeEvent(QCloseEvent *) override;
    void showEvent(QShowEvent *) override;
    bool event(QEvent *) override;

  private:
    SessionController controller;
    QString viewedSession;
    QString displayedSession;
    bool closingAfterFinish = false;
    bool initiallyPositioned = false;
    bool forcePrimaryPlacement = false;
    bool correctingPlacement = false;
    QTimer placementTimer;
    void schedulePlacement(bool forcePrimary = false);
    void ensureOnScreen(bool forcePrimary);
    QComboBox *history, *provider, *remoteDevice, *micDevice;
    QLineEdit *title, *role, *manual;
    ResumeUploadWidget *resume;
    QListWidget *transcripts, *questions;
    QTextBrowser *answer, *report;
    QPushButton *startButton, *pauseButton, *finishButton, *discardButton, *reviewButton, *deleteButton;
    QLabel *state, *remoteStatus, *micStatus, *answerSource;
    QProgressBar *remoteLevel, *micLevel;
    QMediaPlayer player;
    QAudioOutput output;
    QTimer refreshTimer;
    QSet<QString> dirtySessions;
    void refreshHistory();
    void refresh();
    void refreshAnswers();
    void updateState();
    void devices();
    QJsonObject currentPreparation() const;
    QString ensureSession();
    void start();
    void discardRecording();
    void addQuestion();
    void exportMarkdown();
    void playSelected();
    void editTranscript();
    void editQuestion();
};
