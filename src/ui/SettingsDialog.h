#pragma once
#include "ai/TextClient.h"
#include "app/AppSettings.h"
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(QWidget *p = nullptr);

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
    QLineEdit *localModel = nullptr;
    QLabel *planStatus = nullptr;
    QComboBox *deepSeekMode = nullptr;
    QLabel *deepSeekStatus = nullptr;
    void loadDeepSeekModels();
    void reloadAccounts();
    void loadPlanModels();
    bool save();
    void test(int index);
};
