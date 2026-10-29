#pragma once
#include "app/WindowCapture.h"
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
class WindowChrome;
class WrittenInput;
class WrittenInterviewWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit WrittenInterviewWindow(QWidget *owner, WindowCaptureBackend taskbarBackend);
    ~WrittenInterviewWindow() override;
    void setAppearance(bool transparent, int opacity);
    void setPrivacyState(QString status, bool complete, bool systemInteraction);
    void showQuestion(QString id, QString text, QByteArray screenshot = {});
    QString questionId() const {
        return currentQuestion;
    }
    void showAnswer(QString questionId, QString markdown, QString status);
    void acceptQuestion(QString text);
    void acceptImage(QImage image);
    void showOperationStatus(QString message);
    void setShortcutStatus(QString message, bool ready);
    void ensureOnScreen();
    WindowCaptureResult setTaskbarExcluded(bool enabled);
    void setInput(WrittenInput *input);
  signals:
    void questionReady(QString text);
    void imageQuestionReady(QByteArray png, QString prompt);
    void providerSelected(QString provider);
    void returning();
    void taskbarRecreated();

  protected:
    void closeEvent(QCloseEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    bool nativeEvent(const QByteArray &, void *, qintptr *) override;

  private:
    QPlainTextEdit *question;
    QTextBrowser *answer;
    QComboBox *models;
    QLabel *privacy;
    QLabel *imagePreview;
    QLabel *shortcutStatus;
    WindowChrome *chrome;
    WrittenInput *input = nullptr;
    QTimer editDelay;
    QString currentQuestion;
    QString displayedAnswer;
    QString displayedStatus;
    QByteArray screenshot;
    bool glass = false, returningNow = false;
    bool inputStatusActive = false;
    int opacity = 80;
    WindowCaptureBackend shell;
    quintptr restoredTaskbarId = 0;
    void clearTaskbar();
};
