#include "MainWindow.h"
#include "BossVisibility.h"
#include "CapturePrivacy.h"
#include "PlaybackDeviceSelector.h"
#include "PreparationDialog.h"
#include "ResumeUploadWidget.h"
#include "SettingsDialog.h"
#include "WindowAppearance.h"
#include "WindowChrome.h"
#include "WrittenInterviewWindow.h"
#include "ai/InterviewLogic.h"
#include "app/AppSettings.h"
#include "capture/WrittenInput.h"
#include "preparation/Preparation.h"
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequenceEdit>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
namespace {
constexpr int gapMarkerRole = Qt::UserRole + 1;
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
        // the current selection alone would scroll an offscreen selected row into
        // view.
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
void MainWindow::applyAppearance() {
    setPalette(WindowAppearance::palette(transparentBackground));
    setStyleSheet(WindowAppearance::styleSheet(transparentBackground));
    for (auto area : findChildren<QAbstractScrollArea *>())
        area->viewport()->setAutoFillBackground(!transparentBackground);
    if (auto body = findChild<QWidget *>("mainBody"))
        body->setAutoFillBackground(!transparentBackground);
    if (opacitySlider)
        opacitySlider->setEnabled(transparentBackground);
    if (written)
        written->setAppearance(transparentBackground, backgroundOpacity);
    if (auto list = findChild<QListWidget *>("transcriptList"))
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(gapMarkerRole).toBool())
                list->item(i)->setForeground(QColor(transparentBackground ? "#ffc17b" : "#b45309"));
    update();
}
MainWindow::~MainWindow() {
    if (written) {
        auto child = written.data();
        writtenIdentity = nullptr;
        written.clear();
        disconnect(child, nullptr, this, nullptr);
        delete child;
    }
    delete bossKey;
    bossKey = nullptr;
    delete bossVisibility;
    bossVisibility = nullptr;
    // Stop application-wide window callbacks before child widgets are destroyed.
    // Keep native affinity until the corresponding HWND itself is destroyed.
    if (capturePrivacy) {
        disconnect(capturePrivacy, nullptr, this, nullptr);
        delete capturePrivacy;
        capturePrivacy = nullptr;
    }
    clearRestoredTaskbar();
}
void MainWindow::clearRestoredTaskbar() {
    const auto id = restoredTaskbarId;
    restoredTaskbarId = 0;
    const auto invoke = shellBackend;
    if (id && invoke) {
        const auto cleanup = invoke(id, true);
        if (!cleanup.ok)
            qWarning("Taskbar cleanup failed: %s", qPrintable(cleanup.error));
    }
}
void MainWindow::updateCaptureStatus() {
    if (!capturePrivacy || !captureToggle || !captureStatus)
        return;
    const bool on = capturePrivacy->requested();
    const bool complete = capturePrivacy->fullyApplied();
    if (chrome)
        chrome->setSystemInteractionEnabled(!on && complete);
    const QSignalBlocker blocker(captureToggle);
    captureToggle->setChecked(on);
    const auto message = !complete ? QStringLiteral("远程隐藏：%1；%2。向日葵效果需实测")
                                         .arg(on ? QStringLiteral("未完全生效") : QStringLiteral("关闭失败"),
                                              capturePrivacy->error())
                         : on      ? QStringLiteral("远程隐藏：已设置；向日葵效果需实测")
                                   : QStringLiteral("远程隐藏：关闭；向日葵效果需实测");
    captureStatus->setText(message);
    captureStatus->setProperty("privacyError", !complete);
    captureStatus->style()->unpolish(captureStatus);
    captureStatus->style()->polish(captureStatus);
    captureStatus->update();
    if (written)
        written->setPrivacyState(message, complete, !on && complete);
}
void MainWindow::paintEvent(QPaintEvent *event) {
    QMainWindow::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(transparentBackground ? "#73839b" : "#dce3ef"));
    painter.setBrush(WindowAppearance::surface(transparentBackground, backgroundOpacity));
    const auto bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = isMaximized() || isFullScreen() ? 0 : 10;
    painter.drawRoundedRect(bounds, radius, radius);
}
bool MainWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
    if (type == "windows_generic_MSG" && capturePrivacy && capturePrivacy->requested() &&
        !taskbarRefreshPending &&
        WindowCapture::isTaskbarButtonCreatedMessage(message, quintptr(internalWinId()))) {
        taskbarRefreshPending = true;
        QTimer::singleShot(0, this, [this] {
            taskbarRefreshPending = false;
            if (capturePrivacy && capturePrivacy->requested())
                capturePrivacy->setRequested(true);
        });
    }
    return QMainWindow::nativeEvent(type, message, result);
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
bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    if (object == windowHandle() && event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        const QPointer<MainWindow> self(this);
        const QPointer<QObject> receiver(object);
        clearRestoredTaskbar();
        if (!self || !receiver)
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}
void MainWindow::showEvent(QShowEvent *event) {
    applyAppearance();
    QMainWindow::showEvent(event);
    if (windowHandle())
        windowHandle()->installEventFilter(this);
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
    // Leave room for the native title bar and borders, including after DPI
    // changes.
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
MainWindow::MainWindow(QWidget *parent, QString dataRoot, WindowCaptureBackend backend,
                       WindowCaptureBackend taskbarBackend, HotkeyBackend hotkeyBackend)
    : QMainWindow(parent), controller(dataRoot, this), player(this), output(this) {
    writtenHotkeyBackend = hotkeyBackend;
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QApplication::setPalette(WindowAppearance::palette(false));
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setContentsMargins(6, 0, 6, 6);
    chrome = new WindowChrome(this);
    setMenuWidget(chrome);
    transparentBackground = QSettings().value("appearance/transparent", false).toBool();
    bool validOpacity = false;
    const int savedOpacity = QSettings().value("appearance/backgroundOpacity", 80).toInt(&validOpacity);
    backgroundOpacity = validOpacity ? qBound(40, savedOpacity, 95) : 80;
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
    auto body = new QWidget;
    body->setObjectName("mainBody");
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(18, 14, 18, 12);
    layout->setSpacing(10);
    auto viewport = new QScrollArea;
    viewport->setObjectName("mainScrollArea");
    viewport->viewport()->setObjectName("mainViewport");
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
    writtenButton = new QPushButton(QStringLiteral("笔试面试"));
    writtenButton->setObjectName("writtenInterviewButton");
    header->addWidget(writtenButton);
    connect(writtenButton, &QPushButton::clicked, this, &MainWindow::openWrittenInterview);
    hideWindowButton = new QPushButton(QStringLiteral("隐藏窗口"));
    hideWindowButton->setObjectName("hideWindowButton");
    hideWindowButton->setEnabled(false);
    header->addWidget(hideWindowButton);
    auto settings = new QPushButton(QStringLiteral("设置"));
    settings->setObjectName("settingsButton");
    header->addWidget(settings);
    layout->addLayout(header);
    auto appearance = new QHBoxLayout;
    transparentToggle = new QCheckBox(QStringLiteral("透明背景"));
    transparentToggle->setObjectName("transparentBackground");
    transparentToggle->setChecked(transparentBackground);
    transparentToggle->setToolTip(QStringLiteral("只改变背景透明度，文字保持清晰；Ctrl+Shift+O 切换"));
    appearance->addWidget(transparentToggle);
    opacitySlider = new QSlider(Qt::Horizontal);
    opacitySlider->setObjectName("backgroundOpacity");
    opacitySlider->setRange(40, 95);
    opacitySlider->setValue(backgroundOpacity);
    opacitySlider->setFixedWidth(120);
    opacitySlider->setAccessibleName(QStringLiteral("背景不透明度"));
    appearance->addWidget(opacitySlider);
    opacityValue = new QLabel(QStringLiteral("背景不透明度 %1%").arg(backgroundOpacity));
    opacityValue->setObjectName("backgroundOpacityValue");
    appearance->addWidget(opacityValue);
    appearance->addSpacing(14);
    captureToggle = new QPushButton(QStringLiteral("远程隐藏"));
    captureToggle->setObjectName("capturePrivacyToggle");
    captureToggle->setCheckable(true);
    captureToggle->setToolTip(QStringLiteral("本机保持可见，向 Windows 请求捕获排除。"
                                             "向日葵和系统原生弹窗效果需实测；Ctrl+Shift+P 切换。"
                                             "开启时本机和远控端的任务栏按钮一起隐藏，关闭后恢复。"
                                             "最小化后可用 Alt+Tab 找回；已固定的快捷方式需自行取消固定。"
                                             "每次启动默认关闭。"));
    appearance->addWidget(captureToggle);
    captureStatus = new QLabel;
    captureStatus->setObjectName("capturePrivacyStatus");
    captureStatus->setWordWrap(true);
    captureStatus->setMinimumWidth(0);
    captureStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    appearance->addWidget(captureStatus, 1);
    const auto captureBackend = backend ? std::move(backend) : WindowCapture::setExcluded;
    writtenCaptureBackend = captureBackend;
    shellBackend = taskbarBackend ? std::move(taskbarBackend) : WindowCapture::setTaskbarExcluded;
    capturePrivacy = new CapturePrivacy(this, [root = QPointer<MainWindow>(this), captureBackend,
                                               taskbarOperation = shellBackend](quintptr id, bool on) {
        const auto capture = captureBackend(id, on);
        if (!root)
            return capture;
        const bool main = id == quintptr(root->internalWinId());
        const auto child = root->written;
        if (!main && (!child || id != quintptr(child->internalWinId())))
            return capture;
        const bool excludeMain = on || (main && child && !root->isVisible());
        const auto shell = main ? taskbarOperation(id, excludeMain) : child->setTaskbarExcluded(on);
        if (root && shell.ok && main)
            root->restoredTaskbarId = excludeMain ? 0 : id;
        QStringList errors;
        if (!capture.ok)
            errors.append(capture.error.isEmpty() ? QStringLiteral("窗口捕获排除设置失败") : capture.error);
        if (!shell.ok)
            errors.append(shell.error.isEmpty() ? QStringLiteral("任务栏图标设置失败") : shell.error);
        return WindowCaptureResult{errors.isEmpty(), errors.join(QStringLiteral("；"))};
    });
    connect(captureToggle, &QPushButton::toggled, chrome, [chrome = chrome](bool on) {
        if (on)
            chrome->setSystemInteractionEnabled(false);
    });
    connect(captureToggle, &QPushButton::toggled, capturePrivacy, &CapturePrivacy::setRequested);
    connect(capturePrivacy, &CapturePrivacy::stateChanged, this, &MainWindow::updateCaptureStatus);
    updateCaptureStatus();
    layout->addLayout(appearance);
    connect(transparentToggle, &QCheckBox::toggled, this, [this](bool on) {
        transparentBackground = on;
        QSettings().setValue("appearance/transparent", on);
        applyAppearance();
    });
    connect(opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        backgroundOpacity = value;
        QSettings().setValue("appearance/backgroundOpacity", value);
        opacityValue->setText(QStringLiteral("背景不透明度 %1%").arg(value));
        if (written)
            written->setAppearance(transparentBackground, backgroundOpacity);
        update();
    });
    auto appearanceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+O"), this);
    connect(appearanceShortcut, &QShortcut::activated, transparentToggle, &QCheckBox::toggle);
    auto captureShortcut = new QShortcut(QKeySequence("Ctrl+Shift+P"), this);
    connect(captureShortcut, &QShortcut::activated, captureToggle, &QPushButton::toggle);
    applyAppearance();
    connect(provider, &QComboBox::currentTextChanged, this, [](QString p) { AppSettings().select(p); });
    connect(settings, &QPushButton::clicked, this, [this] {
        SettingsDialog dialog(this, bossKey);
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
    resume = new ResumeUploadWidget;
    sessionLayout->addWidget(resume);
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
    remoteDevice->setObjectName("remotePlaybackDevice");
    remoteDevice->setToolTip(QStringLiteral("选择 BOSS / 会议浏览器实际使用的耳机或扬声器。系统默认设备在"
                                            "开始录音时确定，更换设备后请重新开始录音。"));
    connect(remoteDevice, &QComboBox::activated, this, [this] {
        QSettings().setValue("audio/remotePlaybackDevice", remoteDevice->currentData().toString());
    });
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
    connect(&controller, &SessionController::status, this, [this](QString message) {
        statusBar()->showMessage(message);
        if (written)
            written->showOperationStatus(message);
    });
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
        resume->setDocument(previousPreparation["resume"].toObject(),
                            previousPreparation["experience"].toString());
    }
    connect(resume, &ResumeUploadWidget::documentChanged, this, [this] {
        QString error;
        if (!Preparation::save(controller.store.root(), currentPreparation(), &error))
            statusBar()->showMessage(error);
        else
            statusBar()->showMessage(QStringLiteral("简历准备资料已保存；请重新分析公司与岗位"));
    });
    connect(resume, &ResumeUploadWidget::busyChanged, this, [this, preparationButton](bool busy) {
        preparationButton->setEnabled(!busy);
        updateState();
    });
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
            resume->setDocument(saved["resume"].toObject(), saved["experience"].toString());
        }
    });
    refreshHistory();
    refresh();
    updateState();
    statusBar()->showMessage(QStringLiteral("就绪。可先在设置中测试文本模型和语音转写连接。"));
    bossVisibility = new BossVisibility(this);
    bossKey = new GlobalHotkey(this, std::move(hotkeyBackend));
    auto updateBossKey = [this] {
        hideWindowButton->setEnabled(bossKey->registered());
        hideWindowButton->setToolTip(bossKey->registered()
                                         ? QStringLiteral("隐藏本机窗口；按 %1 调出，录音和转写继续")
                                               .arg(bossKey->sequence().toString(QKeySequence::NativeText))
                                         : QStringLiteral("请先在设置 → 快捷键中保存一组可用的老板键"));
    };
    connect(bossKey, &GlobalHotkey::changed, this, updateBossKey);
    connect(bossKey, &GlobalHotkey::activated, this, [this] {
        for (auto focus = QApplication::focusWidget(); focus; focus = focus->parentWidget())
            if (qobject_cast<QKeySequenceEdit *>(focus))
                return; // Do not hide the settings while the user is recording a key.
        bossVisibility->toggle();
    });
    connect(hideWindowButton, &QPushButton::clicked, bossVisibility, &BossVisibility::toggle);
    connect(bossVisibility, &BossVisibility::restored, this, [this] {
        if (written)
            written->ensureOnScreen();
        else
            schedulePlacement();
    });
    const auto savedKey =
        QSettings()
            .value("shortcuts/bossKey", GlobalHotkey::defaultSequence().toString(QKeySequence::PortableText))
            .toString();
    const auto registration =
        bossKey->setSequence(QKeySequence::fromString(savedKey, QKeySequence::PortableText));
    updateBossKey();
    if (!registration.ok)
        statusBar()->showMessage(
            QStringLiteral("老板键未生效：%1。请在设置 → 快捷键中更改").arg(registration.error));
}
void MainWindow::openWrittenInterview() {
    if (written || resume->busy())
        return;
    const bool maximized = isMaximized();
    const QPointer<MainWindow> self(this);
    const auto bounds = geometry();
    written = new WrittenInterviewWindow(this, shellBackend);
    auto child = written.data();
    writtenIdentity = child;
    const QPointer<WrittenInterviewWindow> guard(child);
    connect(child, &QObject::destroyed, this, [this, child] { restoreFromWritten(child); });
    child->setAppearance(transparentBackground, backgroundOpacity);
    child->setGeometry(bounds);
    if (auto item = questions->currentItem()) {
        QString error;
        const auto id = item->data(Qt::UserRole).toString();
        child->showQuestion(id, item->text(), controller.screenshotForQuestion(viewedSession, id, &error));
        if (!error.isEmpty())
            child->showOperationStatus(error);
    }
    refreshWrittenAnswer();
    connect(child, &WrittenInterviewWindow::questionReady, this, &MainWindow::generateWrittenAnswer);
    connect(child, &WrittenInterviewWindow::imageQuestionReady, this, &MainWindow::generateWrittenImage);
    connect(child, &WrittenInterviewWindow::providerSelected, provider, &QComboBox::setCurrentText);
    connect(child, &WrittenInterviewWindow::taskbarRecreated, this,
            [this, guard = QPointer<WrittenInterviewWindow>(child)] {
                if (!guard || guard != written)
                    return;
                if (capturePrivacy->requested())
                    capturePrivacy->setRequested(true);
                else {
                    const auto result = guard->setTaskbarExcluded(false);
                    if (guard && !result.ok)
                        guard->showOperationStatus(result.error);
                }
            });
    connect(child, &WrittenInterviewWindow::returning, this, [this, child] { restoreFromWritten(child); });
    auto appearanceShortcut = new QShortcut(QKeySequence("Ctrl+Shift+O"), child);
    connect(appearanceShortcut, &QShortcut::activated, transparentToggle, &QCheckBox::toggle);
    auto privacyShortcut = new QShortcut(QKeySequence("Ctrl+Shift+P"), child);
    connect(privacyShortcut, &QShortcut::activated, captureToggle, &QPushButton::toggle);
    bossVisibility->setPrimaryWindow(child);
    hide();
    clearRestoredTaskbar();
    if (!self || !guard)
        return;
    if (maximized)
        child->showMaximized();
    else
        child->show();
    if (!guard || written != guard)
        return;
    if (!capturePrivacy->requested()) {
        const auto result = child->setTaskbarExcluded(false);
        if (!guard || written != guard)
            return;
        if (!result.ok)
            guard->showOperationStatus(result.error);
    }
    updateCaptureStatus();
    if (!guard || written != guard)
        return;
    auto input = new WrittenInput(child, writtenHotkeyBackend, {}, writtenCaptureBackend);
    child->setInput(input);
    connect(input, &WrittenInput::questionCaptured, child, &WrittenInterviewWindow::acceptQuestion);
    connect(input, &WrittenInput::imageCaptured, child, &WrittenInterviewWindow::acceptImage);
    connect(input, &WrittenInput::status, child, &WrittenInterviewWindow::showOperationStatus);
    connect(input, &WrittenInput::shortcutStatus, child, &WrittenInterviewWindow::setShortcutStatus);
    input->start();
}
void MainWindow::restoreFromWritten(WrittenInterviewWindow *child) {
    if (writtenIdentity != child)
        return;
    writtenIdentity = nullptr;
    written.clear();
    bossVisibility->setPrimaryWindow(this);
    const QPointer<MainWindow> self(this);
    show();
    if (!self)
        return;
    raise();
    activateWindow();
    if (!capturePrivacy->requested() && internalWinId()) {
        const auto id = quintptr(internalWinId());
        const auto invoke = shellBackend;
        const auto result = invoke(id, false);
        if (!self)
            return;
        if (result.ok && id == quintptr(internalWinId()))
            restoredTaskbarId = id;
        else if (!result.ok)
            statusBar()->showMessage(result.error);
    }
    schedulePlacement();
}
void MainWindow::generateWrittenAnswer(QString text) {
    if (!written || resume->busy())
        return;
    const auto sid = ensureSession();
    if (sid.isEmpty()) {
        written->showOperationStatus(QStringLiteral("无法创建会话"));
        return;
    }
    QString id;
    if (auto selected = questions->currentItem(); selected && selected->text() == text) {
        const auto candidate = selected->data(Qt::UserRole).toString();
        for (const auto &row : controller.store.list("questions", sid))
            if (row["id"].toString() == candidate && !row.contains("screenshot")) {
                id = candidate;
                break;
            }
    }
    if (id.isEmpty())
        id = controller.addQuestion(sid, text, false);
    if (id.isEmpty()) {
        written->showOperationStatus(QStringLiteral("无法保存题目"));
        return;
    }
    written->showQuestion(id, text);
    controller.answerQuestion(sid, id, AppSettings().textConfig());
    refresh();
    for (int i = 0; i < questions->count(); ++i)
        if (questions->item(i)->data(Qt::UserRole).toString() == id)
            questions->setCurrentRow(i);
    refreshWrittenAnswer();
}
void MainWindow::generateWrittenImage(QByteArray png, QString prompt) {
    if (!written || resume->busy())
        return;
    if (prompt.size() > 12000) {
        written->showOperationStatus(QStringLiteral("补充问题超过12000字，请精简"));
        return;
    }
    if (prompt.trimmed().isEmpty())
        prompt = QStringLiteral("请直接回答截图中的题目。");
    const auto sid = ensureSession();
    if (sid.isEmpty())
        return;
    QString id = written->questionId();
    bool same = false;
    for (const auto &q : controller.store.list("questions", sid))
        if (q["id"].toString() == id && q["text"].toString() == prompt)
            same = q["screenshot"].toObject()["sha256"].toString() ==
                   QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
    if (!same)
        id = controller.addScreenshotQuestion(sid, png, prompt);
    if (id.isEmpty()) {
        written->showOperationStatus(QStringLiteral("无法保存截图题目，请检查数据目录"));
        return;
    }
    written->showQuestion(id, prompt, png);
    controller.answerQuestion(sid, id, AppSettings().textConfig());
    refresh();
    for (int i = 0; i < questions->count(); ++i)
        if (questions->item(i)->data(Qt::UserRole).toString() == id)
            questions->setCurrentRow(i);
    refreshWrittenAnswer();
}
void MainWindow::refreshWrittenAnswer() {
    if (!written || written->questionId().isEmpty())
        return;
    QJsonObject latest;
    for (const auto &row : controller.store.list("generations", viewedSession))
        if (row["questionId"].toString() == written->questionId())
            latest = row;
    if (latest.isEmpty())
        return;
    const auto error = latest["error"].toString();
    written->showAnswer(written->questionId(),
                        latest["text"].toString() + (error.isEmpty() ? QString() : "\n\n> " + error),
                        QStringLiteral("%1 / %2 · %3")
                            .arg(latest["provider"].toString(), latest["model"].toString(),
                                 stateName(latest["state"].toString())));
}
void MainWindow::devices() {
    if (controller.recording())
        return;
    micDevice->clear();
    populatePlaybackDevices(*remoteDevice, WasapiLoopbackSource::devices(),
                            QSettings().value("audio/remotePlaybackDevice").toString());
    for (auto d : QMediaDevices::audioInputs())
        micDevice->addItem(d.description(), d.id());
    if (micDevice->count() == 0)
        micDevice->addItem(QStringLiteral("未找到麦克风"), QByteArray());
}
QJsonObject MainWindow::currentPreparation() const {
    auto prepared = Preparation::load(controller.store.root());
    prepared["role"] = role->text();
    prepared["resume"] = resume->document();
    prepared["experience"] = QString();
    return prepared;
}
QString MainWindow::ensureSession() {
    if (!viewedSession.isEmpty())
        return viewedSession;
    const auto prepared = currentPreparation();
    viewedSession =
        controller.createSession(title->text(), role->text(), Preparation::candidateText(prepared), prepared);
    refreshHistory();
    return viewedSession;
}
void MainWindow::start() {
    if (controller.recording() || resume->busy())
        return;
    const auto prepared = currentPreparation();
    auto sid =
        controller.createSession(title->text(), role->text(), Preparation::candidateText(prepared), prepared);
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
    if (resume->busy()) {
        statusBar()->showMessage(QStringLiteral("正在读取简历，请完成后再添加问题"));
        return;
    }
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
        // Old versions saved one row per audio callback. Merge only the
        // presentation, retaining the original database records and separate
        // channel/pause boundaries.
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
            item->setData(gapMarkerRole, true);
            item->setForeground(QColor(transparentBackground ? "#ffc17b" : "#b45309"));
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
    refreshWrittenAnswer();
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
    startButton->setEnabled(!live && !resume->busy());
    writtenButton->setEnabled(!resume->busy());
    resume->setReadOnly(live);
    pauseButton->setEnabled(live && !controller.stopping());
    finishButton->setEnabled(live && !controller.stopping());
    discardButton->setEnabled(live && !controller.stopping());
    pauseButton->setText(controller.paused() ? QStringLiteral("继续录音") : QStringLiteral("暂停"));
    state->setText(controller.stopping() ? QStringLiteral("正在保存最终转写…")
                   : live ? (controller.paused() ? QStringLiteral("已暂停") : QStringLiteral("● 正在录音"))
                          : QStringLiteral("未在录音"));
    for (auto widget : {static_cast<QWidget *>(title), static_cast<QWidget *>(role),
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
