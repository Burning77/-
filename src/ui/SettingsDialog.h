#pragma once
#include "ai/TextClient.h"
#include "app/AppSettings.h"
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QKeySequence>
class GlobalHotkey;
class QKeySequenceEdit;
class QTabWidget;
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(QWidget *p = nullptr, GlobalHotkey *hotkey = nullptr);

  private:
    struct Fields {
        QString provider;
        QLineEdit *url, *model, *key;
        QLabel *result;
    };
    QList<Fields> fields;
    QComboBox *selected;
    TextClient client;
    QComboBox *accounts = nullptr, *planModels = nullptr, *speechMode = nullptr;
    QComboBox *reasoningEffort = nullptr;
    QLineEdit *localModel = nullptr;
    QLabel *planStatus = nullptr;
    QComboBox *deepSeekMode = nullptr;
    QLabel *deepSeekStatus = nullptr;
    GlobalHotkey *bossKey = nullptr;
    QKeySequenceEdit *bossKeyEditor = nullptr;
    QLabel *bossKeyStatus = nullptr;
    QTabWidget *pages = nullptr;
    int shortcutsPage = -1;
    QKeySequence savedBossKey;
    QKeySequenceEdit *selectionCopyEditor = nullptr, *regionCaptureEditor = nullptr;
    QLabel *captureShortcutStatus = nullptr;
    QKeySequence savedSelectionCopy, savedRegionCapture;
    bool saveCaptureShortcuts();
    bool saveBossKey();
    void loadDeepSeekModels();
    void reloadAccounts();
    void loadPlanModels();
    bool save();
    void test(int index);
};
