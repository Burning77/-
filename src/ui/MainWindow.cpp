#include "MainWindow.h"
#include "SettingsDialog.h"
#include "PreparationDialog.h"
#include "preparation/Preparation.h"
#include "ai/InterviewLogic.h"
#include "app/AppSettings.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMediaDevices>
#include <QMessageBox>
#include <QSaveFile>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>
#include <QScrollArea>
#include <algorithm>
namespace {
struct ListScrollPosition {
    int x;
    int y;
    bool follow;

    explicit ListScrollPosition(QListWidget *list, bool reset)
        : x(reset ? 0 : list->horizontalScrollBar()->value()),
          y(reset ? 0 : list->verticalScrollBar()->value()),
          follow(reset || list->verticalScrollBar()->value() == list->verticalScrollBar()->maximum()) {}

    void restore(QListWidget *list) const {
        // Finish the rebuilt items' layout before restoring the viewport. Restoring
        // the current selection alone would scroll an offscreen selected row into view.
        list->doItemsLayout();
        if (follow)
            list->scrollToBottom();
        else
            list->verticalScrollBar()->setValue(y);
        list->horizontalScrollBar()->setValue(x);
    }
};
} // namespace
static QString timestamp(qint64 ms) {
    auto sec = ms / 1000;
    return QString("%1:%2:%3")
        .arg(sec / 3600, 2, 10, QChar('0'))
        .arg((sec / 60) % 60, 2, 10, QChar('0'))
        .arg(sec % 60, 2, 10, QChar('0'));
}
void MainWindow::schedulePlacement(bool forcePrimary) {
    if (correctingPlacement)
        return;
    forcePrimaryPlacement |= forcePrimary;
    if (!placementTimer.isActive())
        placementTimer.start();
}
bool MainWindow::event(QEvent *event) {
    auto type = event->type();
    bool handled = QMainWindow::event(event);
    if (type == QEvent::WindowActivate || type == QEvent::WindowStateChange)
        schedulePlacement();
    return handled;
}
void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    const bool first = !initiallyPositioned;
    initiallyPositioned = true;
    if (first && windowHandle()) {
        if (auto primary = QGuiApplication::primaryScreen())
            windowHandle()->setScreen(primary);
        connect(windowHandle(), &QWindow::screenChanged, this, [this] { schedulePlacement(); });
    }
    schedulePlacement(first);
}
void MainWindow::ensureOnScreen(bool forcePrimary) {
    auto native = windowHandle();
    auto primary = QGuiApplication::primaryScreen();
    if (!native || !primary || !isVisible() || isMinimized())
        return;
    const bool specialState = isMaximized() || isFullScreen() ||
                              native->windowState() == Qt::WindowMaximized ||
                              native->windowState() == Qt::WindowFullScreen;
    for (auto screen : QGuiApplication::screens()) {
        if (specialState && screen->geometry().contains(native->frameGeometry().center()) &&
            screen->geometry().contains(frameGeometry().center()))
            return;
        if (!specialState && !forcePrimary && screen->availableGeometry().contains(native->frameGeometry()) &&
            screen->availableGeometry().contains(frameGeometry()))
            return;
    }
    correctingPlacement = true;
    if (specialState) {
        showNormal();
        native->setWindowState(Qt::WindowNoState);
    }
    native->setScreen(primary);
    const auto available = primary->availableGeometry();
    // Leave room for the native title bar and borders, including after DPI changes.
    QSize limit(qMin(1280, available.width() * 85 / 100), qMin(780, available.height() * 85 / 100));
    resize(forcePrimary ? limit : size().boundedTo(limit));
    auto frame = native->frameGeometry();
    frame.moveCenter(available.center());
    native->setFramePosition(frame.topLeft());
    correctingPlacement = false;
}
static QString stateName(QString s) {
    static QHash<QString, QString> labels{
        {"notes", QStringLiteral("练习记录")},   {"recording", QStringLiteral("录音中")},
        {"paused", QStringLiteral("已暂停")},    {"stopping", QStringLiteral("正在结束")},
        {"finished", QStringLiteral("已结束")},  {"interrupted", QStringLiteral("上次运行中断")},
        {"Queued", QStringLiteral("排队中")},    {"Running", QStringLiteral("正在生成")},
        {"Completed", QStringLiteral("已完成")}, {"Failed", QStringLiteral("未完成")},
        {"Cancelled", QStringLiteral("已停止")}, {"discardFailed", QStringLiteral("丢弃失败")}};
    return labels.value(s, s);
}
MainWindow::MainWindow(QWidget *parent, QString dataRoot)
    : QMainWindow(parent), controller(dataRoot, this), player(this), output(this) {
    setWindowTitle(QStringLiteral("面试助手 · 实时记录与复盘 · ") + QStringLiteral(INTERVIEW_APP_VERSION));
    resize(1280, 780);
    placementTimer.setSingleShot(true);
    placementTimer.setInterval(0);
    connect(&placementTimer, &QTimer::timeout, this, [this] {
        const bool force = forcePrimaryPlacement;
        forcePrimaryPlacement = false;
        ensureOnScreen(force);
    });
    auto watchScreen = [this](QScreen *screen) {
        connect(screen, &QScreen::availableGeometryChanged, this, [this] { schedulePlacement(); });
        connect(screen, &QScreen::geometryChanged, this, [this] { schedulePlacement(); });
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { schedulePlacement(); });
    };
    for (auto screen : QGuiApplication::screens())
        watchScreen(screen);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this, watchScreen](QScreen *screen) {
        watchScreen(screen);
        schedulePlacement();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] { schedulePlacement(); });
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this] { schedulePlacement(); });
    setStyleSheet(QStringLiteral(
        "QMainWindow{background:#f3f6fb} QWidget{font-family:'Microsoft YaHei "
        "UI';font-size:13px;color:#243047} QGroupBox{background:white;border:1px solid "
        "#dce3ef;border-radius:8px;margin-top:14px;padding-top:12px} "
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 5px;color:#364661;font-weight:600} "
        "QPushButton{background:white;border:1px solid #ccd6e5;border-radius:5px;padding:7px 12px} "
        "QPushButton:hover{background:#eef4ff} QPushButton:disabled{color:#a4adba;background:#f1f4f8} "
        "QPushButton#startButton{background:#2563eb;color:white;border:0} "
        "QLineEdit,QPlainTextEdit,QComboBox,QListWidget,QTextBrowser{background:white;border:1px solid "
        "#d7dfeb;border-radius:5px;padding:5px;selection-background-color:#dbeafe;selection-color:#17396d} "
        "QListWidget::item{padding:9px;border-bottom:1px solid #edf0f5} "
        "QProgressBar{border:0;background:#e4eaf3;border-radius:3px;height:7px} "
        "QProgressBar::chunk{background:#3b82f6;border-radius:3px} QTabWidget::pane{border:0} "
        "QLabel#caption{font-size:23px;font-weight:700;color:#152b4f}"));
    auto body = new QWidget;
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(18, 14, 18, 12);
    layout->setSpacing(10);
    auto viewport = new QScrollArea;
    viewport->setFrameShape(QFrame::NoFrame);
    viewport->setWidgetResizable(true);
    viewport->setWidget(body);
    setCentralWidget(viewport);
    auto header = new QHBoxLayout;
    auto caption = new QLabel(QStringLiteral("面试助手"));
    caption->setObjectName("caption");
    header->addWidget(caption);
    header->addWidget(new QLabel(QStringLiteral("实时记录 · 参考回答 · 事后复盘")));
    header->addStretch();
    header->addWidget(new QLabel(QStringLiteral("文本服务")));
    provider = new QComboBox;
    provider->setObjectName("providerCombo");
    provider->addItems({"ChatGPT", "OpenAI", "DeepSeek"});
    provider->setCurrentText(AppSettings().textConfig().provider);
    header->addWidget(provider);
    auto preparationButton = new QPushButton(QStringLiteral("面试准备"));
    preparationButton->setObjectName("preparationButton");
    header->addWidget(preparationButton);
    auto settings = new QPushButton(QStringLiteral("设置"));
    settings->setObjectName("settingsButton");
    header->addWidget(settings);
    layout->addLayout(header);
    connect(provider, &QComboBox::currentTextChanged, this, [](QString p) { AppSettings().select(p); });
    connect(settings, &QPushButton::clicked, this, [this] {
        SettingsDialog dialog(this);
        dialog.exec();
        provider->setCurrentText(AppSettings().textConfig().provider);
    });
    auto sessionBox = new QGroupBox(QStringLiteral("面试会话"));
    auto sessionLayout = new QVBoxLayout(sessionBox);
    auto row = new QHBoxLayout;
    title = new QLineEdit(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd 面试")));
    role = new QLineEdit;
    role->setPlaceholderText(QStringLiteral("目标岗位，如 C++ / 嵌入式工程师"));
    row->addWidget(new QLabel(QStringLiteral("名称")));
    row->addWidget(title, 2);
    row->addWidget(new QLabel(QStringLiteral("岗位")));
    row->addWidget(role, 2);
    history = new QComboBox;
    history->setMinimumWidth(220);
    row->addWidget(new QLabel(QStringLiteral("历史")));
    row->addWidget(history, 2);
    auto newButton = new QPushButton(QStringLiteral("新会话"));
    row->addWidget(newButton);
    sessionLayout->addLayout(row);
    experience = new QPlainTextEdit;
    experience->setMaximumHeight(60);
    experience->setPlaceholderText(
        QStringLiteral("可选：填写真实项目经历、技术栈和职责，帮助参考回答贴合你的背景。"));
    sessionLayout->addWidget(experience);
    auto controls = new QHBoxLayout;
    startButton = new QPushButton(QStringLiteral("开始面试"));
    startButton->setObjectName("startButton");
    pauseButton = new QPushButton(QStringLiteral("暂停"));
    finishButton = new QPushButton(QStringLiteral("结束并保存"));
    discardButton = new QPushButton(QStringLiteral("结束不保存"));
    discardButton->setObjectName("discardButton");
    state = new QLabel(QStringLiteral("未开始"));
    controls->addWidget(startButton);
    controls->addWidget(pauseButton);
    controls->addWidget(finishButton);
    controls->addWidget(discardButton);
    controls->addWidget(state);
    controls->addStretch();
    auto automatic = new QCheckBox(QStringLiteral("自动提取问题"));
    automatic->setChecked(true);
    auto autoAnswer = new QCheckBox(QStringLiteral("自动生成参考回答"));
    autoAnswer->setChecked(true);
    controls->addWidget(automatic);
    controls->addWidget(autoAnswer);
    sessionLayout->addLayout(controls);
    layout->addWidget(sessionBox);
    connect(automatic, &QCheckBox::toggled, this, [this](bool b) { controller.autoExtract = b; });
    connect(autoAnswer, &QCheckBox::toggled, this, [this](bool b) { controller.autoAnswer = b; });
    auto audioRow = new QHBoxLayout;
    remoteDevice = new QComboBox;
    micDevice = new QComboBox;
    remoteStatus = new QLabel(QStringLiteral("远端：待连接"));
    micStatus = new QLabel(QStringLiteral("麦克风：待连接"));
    remoteLevel = new QProgressBar;
    micLevel = new QProgressBar;
    for (auto level : {remoteLevel, micLevel}) {
        level->setRange(0, 100);
        level->setTextVisible(false);
        level->setMaximumHeight(7);
    }
    auto remoteColumn = new QVBoxLayout;
    auto micColumn = new QVBoxLayout;
    auto remoteControls = new QHBoxLayout;
    remoteControls->addWidget(new QLabel(QStringLiteral("远端播放设备")));
    remoteControls->addWidget(remoteDevice, 1);
    remoteColumn->addLayout(remoteControls);
    remoteColumn->addWidget(remoteLevel);
    remoteColumn->addWidget(remoteStatus);
    auto micControls = new QHBoxLayout;
    micControls->addWidget(new QLabel(QStringLiteral("我的麦克风")));
    micControls->addWidget(micDevice, 1);
    micColumn->addLayout(micControls);
    micColumn->addWidget(micLevel);
    micColumn->addWidget(micStatus);
    audioRow->addLayout(remoteColumn, 1);
    audioRow->addSpacing(16);
    audioRow->addLayout(micColumn, 1);
    auto reload = new QPushButton(QStringLiteral("刷新设备"));
    audioRow->addWidget(reload);
    layout->addLayout(audioRow);
    connect(reload, &QPushButton::clicked, this, &MainWindow::devices);
    devices();
    auto splitter = new QSplitter;
    layout->addWidget(splitter, 1);
    auto conversation = new QGroupBox(QStringLiteral("实时对话 · 双击可修正"));
    auto left = new QVBoxLayout(conversation);
    transcripts = new QListWidget;
    transcripts->setObjectName("transcriptList");
    transcripts->setWordWrap(true);
    left->addWidget(transcripts);
    auto playbackRow = new QHBoxLayout;
    auto playback = new QPushButton(QStringLiteral("回听所选片段"));
    auto stopPlayback = new QPushButton(QStringLiteral("停止播放"));
    playbackRow->addWidget(playback);
    playbackRow->addWidget(stopPlayback);
    left->addLayout(playbackRow);
    left->addWidget(new QLabel(QStringLiteral("时间为近似片段位置；耳机有助于减少回录。")));
    splitter->addWidget(conversation);
    auto questionBox = new QGroupBox(QStringLiteral("面试官问题"));
    auto middle = new QVBoxLayout(questionBox);
    questions = new QListWidget;
    questions->setWordWrap(true);
    middle->addWidget(questions);
    auto extract = new QPushButton(QStringLiteral("立即提取问题"));
    auto regenerate = new QPushButton(QStringLiteral("重新生成所选回答"));
    auto edit = new QPushButton(QStringLiteral("编辑问题"));
    middle->addWidget(extract);
    middle->addWidget(regenerate);
    middle->addWidget(edit);
    splitter->addWidget(questionBox);
    auto resultBox = new QGroupBox(QStringLiteral("分析与建议"));
    auto right = new QVBoxLayout(resultBox);
    auto tabs = new QTabWidget;
    auto answerPage = new QWidget;
    auto answerLayout = new QVBoxLayout(answerPage);
    answerSource = new QLabel(QStringLiteral("选择问题后显示参考回答"));
    answerSource->setWordWrap(true);
    answerLayout->addWidget(answerSource);
    answer = new QTextBrowser;
    answer->setOpenExternalLinks(false);
    answerLayout->addWidget(answer);
    auto stop = new QPushButton(QStringLiteral("停止当前会话的生成"));
    answerLayout->addWidget(stop);
    tabs->addTab(answerPage, QStringLiteral("参考回答"));
    auto reviewPage = new QWidget;
    auto reviewLayout = new QVBoxLayout(reviewPage);
    report = new QTextBrowser;
    report->setOpenExternalLinks(false);
    reviewButton = new QPushButton(QStringLiteral("生成事后复盘"));
    reviewLayout->addWidget(reviewButton);
    reviewLayout->addWidget(report);
    tabs->addTab(reviewPage, QStringLiteral("复盘建议"));
    right->addWidget(tabs);
    splitter->addWidget(resultBox);
    splitter->setSizes({460, 300, 540});
    auto manualRow = new QHBoxLayout;
    manual = new QLineEdit;
    manual->setPlaceholderText(QStringLiteral("手动输入问题；不录音也可进行面试练习"));
    auto submit = new QPushButton(QStringLiteral("添加问题并分析"));
    manualRow->addWidget(manual, 1);
    manualRow->addWidget(submit);
    layout->addLayout(manualRow);
    auto bottom = new QHBoxLayout;
    auto exportButton = new QPushButton(QStringLiteral("导出 Markdown"));
    deleteButton = new QPushButton(QStringLiteral("删除此会话"));
    auto folder = new QPushButton(QStringLiteral("打开数据目录"));
    bottom->addWidget(exportButton);
    bottom->addWidget(deleteButton);
    bottom->addWidget(folder);
    bottom->addStretch();
    auto note = new QLabel(QStringLiteral("录音及 AI 辅助请事先获得参与者同意。"));
    bottom->addWidget(note);
    layout->addLayout(bottom);
    player.setAudioOutput(&output);
    output.setVolume(0.7);
    connect(&player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, QString message) {
        statusBar()->showMessage(QStringLiteral("播放失败：") + message);
    });
    connect(startButton, &QPushButton::clicked, this, &MainWindow::start);
    connect(pauseButton, &QPushButton::clicked, &controller, &SessionController::togglePause);
    connect(finishButton, &QPushButton::clicked, &controller, &SessionController::finish);
    connect(newButton, &QPushButton::clicked, this, [this] {
        viewedSession.clear();
        title->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd 面试")));
        refreshHistory();
        refresh();
    });
    connect(history, &QComboBox::currentIndexChanged, this, [this](int index) {
        viewedSession = history->itemData(index).toString();
        refresh();
    });
    connect(&controller, &SessionController::changed, this, [this](QString sid) {
        dirtySessions.insert(sid);
        if (!refreshTimer.isActive())
            refreshTimer.start();
    });
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(100);
    connect(&refreshTimer, &QTimer::timeout, this, [this] {
        const bool currentChanged = dirtySessions.contains(viewedSession);
        dirtySessions.clear();
        refreshHistory();
        if (currentChanged)
            refresh();
    });
    connect(&controller, &SessionController::status, this,
            [this](QString message) { statusBar()->showMessage(message); });
    connect(&controller, &SessionController::transcriptionStatus, this, [this](QString ch, QString message) {
        (ch == "remote" ? remoteStatus : micStatus)
            ->setText((ch == "remote" ? QStringLiteral("远端：") : QStringLiteral("麦克风：")) + message);
    });
    connect(&controller, &SessionController::stateChanged, this, [this] {
        updateState();
        if (closingAfterFinish && !controller.recording())
            close();
    });
    connect(&controller.audio, &AudioController::levelChanged, this, [this](QString ch, float level) {
        (ch == "remote" ? remoteLevel : micLevel)->setValue(qBound(0, int(level * 250), 100));
    });
    connect(submit, &QPushButton::clicked, this, &MainWindow::addQuestion);
    connect(discardButton, &QPushButton::clicked, this, &MainWindow::discardRecording);
    connect(manual, &QLineEdit::returnPressed, this, &MainWindow::addQuestion);
    connect(extract, &QPushButton::clicked, this, [this] { controller.extractNow(viewedSession); });
    connect(regenerate, &QPushButton::clicked, this, [this] {
        if (auto item = questions->currentItem())
            controller.answerQuestion(viewedSession, item->data(Qt::UserRole).toString(),
                                      AppSettings().textConfig());
    });
    connect(edit, &QPushButton::clicked, this, &MainWindow::editQuestion);
    connect(questions, &QListWidget::itemDoubleClicked, this, [this] { editQuestion(); });
    connect(questions, &QListWidget::currentItemChanged, this, [this] { refreshAnswers(); });
    connect(transcripts, &QListWidget::itemDoubleClicked, this, [this] { editTranscript(); });
    connect(playback, &QPushButton::clicked, this, &MainWindow::playSelected);
    connect(stopPlayback, &QPushButton::clicked, &player, &QMediaPlayer::stop);
    connect(stop, &QPushButton::clicked, this, [this] { controller.cancel(viewedSession); });
    connect(reviewButton, &QPushButton::clicked, this, [this] { controller.review(viewedSession); });
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportMarkdown);
    connect(folder, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(controller.store.root())); });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        if (viewedSession.isEmpty())
            return;
        if (controller.recording() && viewedSession == controller.liveSessionId())
            return;
        if (QMessageBox::question(this, QStringLiteral("删除会话"),
                                  QStringLiteral("同时删除该会话的录音、对话和报告？")) != QMessageBox::Yes)
            return;
        controller.cancel(viewedSession);
        player.stop();
        if (!controller.store.removeSession(viewedSession)) {
            QMessageBox::warning(this, QStringLiteral("删除失败"), controller.store.lastError());
            return;
        }
        viewedSession.clear();
        refreshHistory();
        refresh();
    });
    auto previousPreparation = Preparation::load(controller.store.root());
    if (!previousPreparation.isEmpty()) {
        role->setText(previousPreparation["role"].toString());
        experience->setPlainText(previousPreparation["experience"].toString());
    }
    connect(preparationButton, &QPushButton::clicked, this, [this] {
        auto before = Preparation::load(controller.store.root());
        auto prepared = currentPreparation();
        const bool locked = controller.recording();
        if (locked) {
            auto sessions = controller.store.list("sessions", controller.liveSessionId());
            if (!sessions.isEmpty())
                prepared = sessions.first()["preparation"].toObject();
        }
        PreparationDialog dialog(controller.store.root(), prepared, locked, this);
        dialog.exec();
        auto saved = Preparation::load(controller.store.root());
        if (!locked && saved != before) {
            role->setText(saved["role"].toString());
            experience->setPlainText(saved["experience"].toString());
        }
    });
    refreshHistory();
    refresh();
    updateState();
    statusBar()->showMessage(QStringLiteral("就绪。可先在设置中测试文本模型和语音转写连接。"));
}
void MainWindow::devices() {
    if (controller.recording())
        return;
    remoteDevice->clear();
    micDevice->clear();
    for (auto d : WasapiLoopbackSource::devices())
        remoteDevice->addItem(d.name, d.id);
    for (auto d : QMediaDevices::audioInputs())
        micDevice->addItem(d.description(), d.id());
    if (remoteDevice->count() == 0)
        remoteDevice->addItem(QStringLiteral("系统默认播放设备"), QString());
    if (micDevice->count() == 0)
        micDevice->addItem(QStringLiteral("未找到麦克风"), QByteArray());
}
QJsonObject MainWindow::currentPreparation() const {
    auto prepared = Preparation::load(controller.store.root());
    prepared["role"] = role->text();
    prepared["experience"] = experience->toPlainText();
    return prepared;
}
QString MainWindow::ensureSession() {
    if (!viewedSession.isEmpty())
        return viewedSession;
    viewedSession = controller.createSession(title->text(), role->text(), experience->toPlainText(),
                                             currentPreparation());
    refreshHistory();
    return viewedSession;
}
void MainWindow::start() {
    if (controller.recording())
        return;
    auto sid = controller.createSession(title->text(), role->text(), experience->toPlainText(),
                                        currentPreparation());
    if (sid.isEmpty())
        return;
    viewedSession = sid;
    refreshHistory();
    controller.startRecording(sid, remoteDevice->currentData().toString(),
                              micDevice->currentData().toByteArray());
    refresh();
}
void MainWindow::discardRecording() {
    if (!controller.recording() || controller.stopping())
        return;
    const auto sid = controller.liveSessionId();
    QString name;
    for (auto session : controller.store.list("sessions"))
        if (session["id"] == sid)
            name = session["title"].toString();
    if (QMessageBox::question(
            this, QStringLiteral("结束不保存"),
            QStringLiteral("丢弃正在录音的会话“%1”？本次录音、对话和 AI 结果将被删除。").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (controller.liveSessionId() != sid || !controller.recording() || controller.stopping())
        return;
    player.stop();
    const bool removed = controller.discardRecording();
    if (removed && viewedSession == sid)
        viewedSession.clear();
    refreshHistory();
    refresh();
    if (!removed)
        QMessageBox::warning(this, QStringLiteral("丢弃失败"), controller.store.lastError());
}
void MainWindow::addQuestion() {
    auto text = manual->text().trimmed();
    if (text.isEmpty())
        return;
    auto sid = ensureSession();
    if (sid.isEmpty())
        return;
    manual->clear();
    auto id = controller.addQuestion(sid, text);
    refresh();
    for (int i = 0; i < questions->count(); i++)
        if (questions->item(i)->data(Qt::UserRole) == id)
            questions->setCurrentRow(i);
}
void MainWindow::refreshHistory() {
    QSignalBlocker block(history);
    history->clear();
    history->addItem(QStringLiteral("新会话 / 手动练习"), QString());
    auto rows = controller.store.list("sessions");
    for (auto i = rows.crbegin(); i != rows.crend(); ++i)
        history->addItem((*i)["title"].toString() + " · " + stateName((*i)["state"].toString()),
                         (*i)["id"].toString());
    history->setCurrentIndex(qMax(0, history->findData(viewedSession)));
}
void MainWindow::refresh() {
    const bool sessionChanged = displayedSession != viewedSession;
    const ListScrollPosition transcriptScroll(transcripts, sessionChanged);
    const ListScrollPosition questionScroll(questions, sessionChanged);
    displayedSession = viewedSession;
    auto selected =
        transcripts->currentItem() ? transcripts->currentItem()->data(Qt::UserRole).toString() : QString();
    transcripts->clear();
    if (!viewedSession.isEmpty()) {
        auto rows = controller.store.list("transcripts", viewedSession);
        std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) {
            return a["startMs"].toInteger() < b["startMs"].toInteger();
        });
        for (auto row : rows) {
            auto text = QString("%1 · %2%3\n%4")
                            .arg(timestamp(row["startMs"].toInteger()),
                                 row["channel"] == "remote" ? QStringLiteral("面试官 / 远端")
                                                            : QStringLiteral("我 / 麦克风"),
                                 row["final"].toBool() ? QString() : QStringLiteral(" · 转写中"),
                                 row["text"].toString());
            if (row.value("terminologyAdjusted").toBool())
                text += QStringLiteral("\n[术语辅助，原转写：%1]").arg(row["rawText"].toString());
            auto item = new QListWidgetItem(text, transcripts);
            item->setData(Qt::UserRole, row["id"].toString());
            if (row["id"] == selected)
                transcripts->setCurrentItem(item);
        }
        auto gaps = controller.store.list("gaps", viewedSession);
        // Old versions saved one row per audio callback. Merge only the presentation,
        // retaining the original database records and separate channel/pause boundaries.
        std::stable_sort(gaps.begin(), gaps.end(), [](const auto &a, const auto &b) {
            if (a["channel"] != b["channel"])
                return a["channel"].toString() < b["channel"].toString();
            if (a["reason"] != b["reason"])
                return a["reason"].toString() < b["reason"].toString();
            return a["startMs"].toInteger() < b["startMs"].toInteger();
        });
        QList<QJsonObject> merged;
        for (auto gap : gaps) {
            if (!merged.isEmpty() && merged.last()["channel"] == gap["channel"] &&
                merged.last()["reason"] == gap["reason"] &&
                gap["startMs"].toInteger() <= merged.last()["endMs"].toInteger() + 20) {
                merged.last()["endMs"] = qMax(merged.last()["endMs"].toInteger(), gap["endMs"].toInteger());
            } else
                merged.append(gap);
        }
        std::stable_sort(merged.begin(), merged.end(), [](const auto &a, const auto &b) {
            return a["startMs"].toInteger() < b["startMs"].toInteger();
        });
        for (auto gap : merged) {
            auto item = new QListWidgetItem(
                QStringLiteral("⚠ %1–%2 · %3：%4")
                    .arg(timestamp(gap["startMs"].toInteger()), timestamp(gap["endMs"].toInteger()),
                         gap["channel"] == "remote" ? QStringLiteral("远端") : QStringLiteral("麦克风"),
                         gap["reason"] == "paused" ? QStringLiteral("暂停区间")
                                                   : QStringLiteral("转写缺失区间")),
                transcripts);
            item->setForeground(QColor("#b45309"));
        }
    }
    transcriptScroll.restore(transcripts);
    auto qid = questions->currentItem() ? questions->currentItem()->data(Qt::UserRole).toString() : QString();
    {
        QSignalBlocker block(questions);
        questions->clear();
        if (!viewedSession.isEmpty())
            for (auto q : controller.store.list("questions", viewedSession)) {
                auto item = new QListWidgetItem(q["text"].toString(), questions);
                item->setData(Qt::UserRole, q["id"].toString());
                if (q["id"] == qid)
                    questions->setCurrentItem(item);
            }
        if (!questions->currentItem() && questions->count())
            questions->setCurrentRow(questions->count() - 1);
    }
    questionScroll.restore(questions);
    refreshAnswers();
    updateState();
}
void MainWindow::refreshAnswers() {
    QString qid =
        questions->currentItem() ? questions->currentItem()->data(Qt::UserRole).toString() : QString();
    QJsonObject latest;
    if (!viewedSession.isEmpty())
        for (auto a : controller.store.list("generations", viewedSession))
            if (a["questionId"] == qid)
                latest = a;
    if (latest.isEmpty()) {
        answerSource->setText(QStringLiteral("选择问题后显示 AI 参考回答"));
        answer->clear();
    } else {
        answerSource->setText(QStringLiteral("%1 / %2 · 问题版本 %3 · %4")
                                  .arg(latest["provider"].toString(), latest["model"].toString())
                                  .arg(latest["questionRevision"].toInt())
                                  .arg(stateName(latest["state"].toString())));
        int y = answer->verticalScrollBar()->value();
        answer->setMarkdown(latest["text"].toString() + (latest["error"].toString().isEmpty()
                                                             ? QString()
                                                             : "\n\n> " + latest["error"].toString()));
        answer->verticalScrollBar()->setValue(y);
    }
    QString md;
    if (!viewedSession.isEmpty()) {
        auto rows = controller.store.list("reviews", viewedSession);
        QString group;
        if (!rows.isEmpty())
            group = rows.last()["groupId"].toString();
        for (auto r : rows)
            if (r["groupId"] == group)
                md += QStringLiteral("### 第 %1 / %2 部分 · %3 / %4 · %5\n\n%6\n\n%7\n\n")
                          .arg(r["part"].toInt())
                          .arg(r["totalParts"].toInt())
                          .arg(r["provider"].toString(), r["model"].toString(),
                               stateName(r["state"].toString()), r["text"].toString(), r["error"].toString());
    }
    int y = report->verticalScrollBar()->value();
    report->setMarkdown(md);
    report->verticalScrollBar()->setValue(y);
}
void MainWindow::updateState() {
    bool live = controller.recording();
    startButton->setEnabled(!live);
    pauseButton->setEnabled(live && !controller.stopping());
    finishButton->setEnabled(live && !controller.stopping());
    discardButton->setEnabled(live && !controller.stopping());
    pauseButton->setText(controller.paused() ? QStringLiteral("继续录音") : QStringLiteral("暂停"));
    state->setText(controller.stopping() ? QStringLiteral("正在保存最终转写…")
                   : live ? (controller.paused() ? QStringLiteral("已暂停") : QStringLiteral("● 正在录音"))
                          : QStringLiteral("未在录音"));
    for (auto widget :
         {static_cast<QWidget *>(title), static_cast<QWidget *>(role), static_cast<QWidget *>(experience),
          static_cast<QWidget *>(remoteDevice), static_cast<QWidget *>(micDevice)})
        widget->setEnabled(!live);
    reviewButton->setEnabled(!viewedSession.isEmpty() &&
                             !(live && viewedSession == controller.liveSessionId()));
    deleteButton->setEnabled(!viewedSession.isEmpty() &&
                             !(live && viewedSession == controller.liveSessionId()));
}
void MainWindow::exportMarkdown() {
    if (viewedSession.isEmpty())
        return;
    auto file =
        QFileDialog::getSaveFileName(this, QStringLiteral("导出面试记录"), "interview.md", "Markdown (*.md)");
    if (file.isEmpty())
        return;
    QJsonObject session;
    for (auto s : controller.store.list("sessions"))
        if (s["id"] == viewedSession)
            session = s;
    auto md = InterviewLogic::markdown(session, controller.store.list("transcripts", viewedSession),
                                       controller.store.list("questions", viewedSession),
                                       controller.store.list("generations", viewedSession),
                                       controller.store.list("reviews", viewedSession));
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly) || out.write(md.toUtf8()) < 0 || !out.commit())
        QMessageBox::warning(this, QStringLiteral("导出失败"), out.errorString());
    else
        statusBar()->showMessage(QStringLiteral("已导出：") + file);
}
void MainWindow::playSelected() {
    if (controller.recording()) {
        statusBar()->showMessage(QStringLiteral("请先结束录音再回听，避免声音再次录入"));
        return;
    }
    auto item = transcripts->currentItem();
    if (!item)
        return;
    QJsonObject transcript;
    for (auto t : controller.store.list("transcripts", viewedSession))
        if (t["id"] == item->data(Qt::UserRole).toString())
            transcript = t;
    if (transcript.isEmpty())
        return;
    auto time = transcript["startMs"].toInteger();
    for (auto a : controller.store.list("audio", viewedSession))
        if (a["channel"] == transcript["channel"] && time >= a["startMs"].toInteger() &&
            time < a["endMs"].toInteger()) {
            auto path = controller.store.root() + "/" + a["path"].toString();
            if (!QFileInfo::exists(path)) {
                statusBar()->showMessage(QStringLiteral("录音文件已缺失"));
                return;
            }
            player.setSource(QUrl::fromLocalFile(path));
            player.setPosition(time - a["startMs"].toInteger());
            player.play();
            return;
        }
    statusBar()->showMessage(QStringLiteral("没有与所选片段对应的录音文件"));
}
void MainWindow::editTranscript() {
    auto item = transcripts->currentItem();
    if (!item)
        return;
    auto id = item->data(Qt::UserRole).toString();
    const auto sid = viewedSession;
    for (auto t : controller.store.list("transcripts", sid))
        if (t["id"] == id) {
            bool ok = false;
            auto text = QInputDialog::getMultiLineText(this, QStringLiteral("修正转写"),
                                                       QStringLiteral("保存后可重新提取问题"),
                                                       t["text"].toString(), &ok);
            if (ok)
                controller.editTranscript(sid, id, text);
            break;
        }
}
void MainWindow::editQuestion() {
    auto item = questions->currentItem();
    if (!item)
        return;
    const auto sid = viewedSession;
    const auto id = item->data(Qt::UserRole).toString();
    const auto initial = item->text();
    bool ok = false;
    auto text = QInputDialog::getMultiLineText(this, QStringLiteral("编辑问题"),
                                               QStringLiteral("修改后点击重新生成回答"), initial, &ok);
    if (ok)
        controller.editQuestion(sid, id, text);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (controller.recording()) {
        if (controller.stopping()) {
            event->ignore();
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("结束面试"),
                                  QStringLiteral("结束录音并保存后退出？")) != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        closingAfterFinish = true;
        controller.finish();
        event->ignore();
        return;
    }
    event->accept();
}
