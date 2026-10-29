#include "WrittenInterviewWindow.h"
#include "WindowAppearance.h"
#include "WindowChrome.h"
#include "app/AppSettings.h"
#include "capture/WrittenInput.h"
#include <QAbstractScrollArea>
#include <QBuffer>
#include <QCloseEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

WrittenInterviewWindow::WrittenInterviewWindow(QWidget *owner, WindowCaptureBackend backend)
    : QMainWindow(owner, Qt::Window), shell(std::move(backend)) {
    setObjectName("writtenInterviewWindow");
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setWindowTitle(QStringLiteral("笔试面试 · ") + QStringLiteral(INTERVIEW_APP_VERSION));
    setContentsMargins(6, 0, 6, 6);
    chrome = new WindowChrome(this);
    setMenuWidget(chrome);
    auto body = new QWidget;
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(12, 8, 12, 8);
    auto top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("AI 模型")));
    models = new QComboBox;
    models->setObjectName("writtenModelCombo");
    models->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    models->setMinimumContentsLength(20);
    for (const auto &name : {"ChatGPT", "OpenAI", "DeepSeek"}) {
        const auto config = AppSettings().config(name);
        models->addItem(QString::fromLatin1(name) + " / " +
                            (config.model.isEmpty() ? QStringLiteral("未配置模型") : config.model),
                        name);
    }
    models->setCurrentIndex(qMax(0, models->findData(AppSettings().textConfig().provider)));
    top->addWidget(models, 1);
    auto end = new QPushButton(QStringLiteral("结束"));
    end->setObjectName("endWrittenInterview");
    top->addWidget(end);
    layout->addLayout(top);
    auto splitter = new QSplitter(Qt::Vertical);
    splitter->setObjectName("writtenSplitter");
    auto questionPanel = new QGroupBox(QStringLiteral("问题"));
    auto questionLayout = new QVBoxLayout(questionPanel);
    imagePreview = new QLabel;
    imagePreview->setObjectName("writtenImagePreview");
    imagePreview->setAlignment(Qt::AlignCenter);
    imagePreview->setMaximumHeight(210);
    imagePreview->setMinimumHeight(90);
    imagePreview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    imagePreview->hide();
    questionLayout->addWidget(imagePreview);
    question = new QPlainTextEdit;
    question->setObjectName("writtenQuestion");
    question->setPlaceholderText(QStringLiteral(
        "选中文字后按取词快捷键，或按截图快捷键选取题目。也可粘贴或输入；题目进入后自动生成答案。"));
    questionLayout->addWidget(question);
    auto notice =
        new QLabel(QStringLiteral("所选文字或截图原图会发送给所选模型；快捷键可在原窗口设置中更改。"));
    shortcutStatus = notice;
    shortcutStatus->setObjectName("writtenShortcutStatus");
    notice->setWordWrap(true);
    questionLayout->addWidget(notice);
    auto answerPanel = new QGroupBox(QStringLiteral("答案"));
    auto answerLayout = new QVBoxLayout(answerPanel);
    answer = new QTextBrowser;
    answer->setObjectName("writtenAnswer");
    answer->setOpenExternalLinks(false);
    answerLayout->addWidget(answer);
    splitter->addWidget(questionPanel);
    splitter->addWidget(answerPanel);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter, 1);
    setCentralWidget(body);
    privacy = new QLabel;
    privacy->setObjectName("writtenPrivacyStatus");
    privacy->setTextFormat(Qt::PlainText);
    privacy->setWordWrap(false);
    privacy->setMinimumWidth(0);
    privacy->setMaximumWidth(220);
    privacy->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    statusBar()->addPermanentWidget(privacy);
    editDelay.setSingleShot(true);
    editDelay.setInterval(800);
    connect(question, &QPlainTextEdit::textChanged, this, [this] {
        currentQuestion.clear();
        displayedAnswer.clear();
        answer->clear();
        editDelay.start();
    });
    connect(&editDelay, &QTimer::timeout, this, [this] {
        if (screenshot.isEmpty())
            acceptQuestion(question->toPlainText());
        else
            emit imageQuestionReady(screenshot, question->toPlainText());
    });
    connect(models, &QComboBox::currentIndexChanged, this, [this] {
        emit providerSelected(models->currentData().toString());
        editDelay.stop();
        if (!screenshot.isEmpty())
            emit imageQuestionReady(screenshot, question->toPlainText());
        else if (!question->toPlainText().trimmed().isEmpty())
            acceptQuestion(question->toPlainText());
    });
    connect(end, &QPushButton::clicked, this, &QWidget::close);
}
WrittenInterviewWindow::~WrittenInterviewWindow() {
    if (input)
        input->stop();
    clearTaskbar();
}
void WrittenInterviewWindow::setAppearance(bool transparent, int value) {
    glass = transparent;
    opacity = qBound(40, value, 95);
    setPalette(WindowAppearance::palette(glass));
    setStyleSheet(WindowAppearance::styleSheet(glass));
    for (auto area : findChildren<QAbstractScrollArea *>())
        area->viewport()->setAutoFillBackground(!glass);
    update();
}
void WrittenInterviewWindow::setPrivacyState(QString text, bool complete, bool systemInteraction) {
    privacy->setToolTip(text);
    privacy->setText(!complete           ? QStringLiteral("远程隐藏：未完全生效")
                     : systemInteraction ? QStringLiteral("远程隐藏：关闭")
                                         : QStringLiteral("远程隐藏：开启"));
    privacy->setProperty("privacyError", !complete);
    privacy->style()->unpolish(privacy);
    privacy->style()->polish(privacy);
    chrome->setSystemInteractionEnabled(systemInteraction);
}
void WrittenInterviewWindow::showQuestion(QString id, QString text, QByteArray image) {
    inputStatusActive = false;
    displayedStatus.clear();
    editDelay.stop();
    const QSignalBlocker blocker(question);
    if (question->toPlainText() != text)
        question->setPlainText(text);
    if (currentQuestion != id) {
        displayedAnswer.clear();
        displayedStatus.clear();
        answer->clear();
    }
    currentQuestion = id;
    screenshot = std::move(image);
    if (screenshot.isEmpty())
        imagePreview->hide();
    else {
        const auto pixmap = QPixmap::fromImage(QImage::fromData(screenshot, "PNG"));
        imagePreview->setPixmap(pixmap.scaled(QSize(qMax(200, width() - 60), 200), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation));
        imagePreview->show();
    }
}
void WrittenInterviewWindow::acceptImage(QImage image) {
    if (image.isNull()) {
        showOperationStatus(QStringLiteral("截图为空，请重试"));
        return;
    }
    if (qint64(image.width()) * image.height() > 16000000) {
        showOperationStatus(QStringLiteral("截图过大，请缩小区域"));
        return;
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG") || png.size() > 10 * 1024 * 1024) {
        showOperationStatus(QStringLiteral("截图超过10MB或无法编码，请缩小区域"));
        return;
    }
    showQuestion({}, QStringLiteral("请直接回答截图中的题目；如有多道题，请依次给出答案。"), png);
    answer->clear();
    showOperationStatus(QStringLiteral("正在将截图发送给所选模型…"));
    // Commit the preview before file persistence/model dispatch can occupy
    // the event loop. A deferred child update can leave a layered HWND stale.
    const QPointer<WrittenInterviewWindow> self(this);
    repaint();
    if (!self)
        return;
    emit imageQuestionReady(png, question->toPlainText());
}
void WrittenInterviewWindow::showAnswer(QString id, QString markdown, QString status) {
    if (id.isEmpty() || id != currentQuestion)
        return;
    if (status != displayedStatus) {
        displayedStatus = status;
        if (!inputStatusActive)
            statusBar()->showMessage(status);
    }
    if (markdown == displayedAnswer)
        return;
    auto bar = answer->verticalScrollBar();
    const bool follow = bar->value() >= bar->maximum() - 2;
    const int position = bar->value();
    displayedAnswer = markdown;
    answer->setMarkdown(markdown);
    bar->setValue(follow ? bar->maximum() : position);
}
void WrittenInterviewWindow::acceptQuestion(QString text) {
    editDelay.stop();
    text = text.trimmed();
    if (text.isEmpty())
        return;
    if (text.size() > 12000) {
        showOperationStatus(QStringLiteral("题目超过12000字，请缩小选区或精简后重试"));
        return;
    }
    showQuestion({}, text);
    answer->clear();
    showOperationStatus(QStringLiteral("正在生成答案…"));
    const QPointer<WrittenInterviewWindow> self(this);
    repaint();
    if (!self)
        return;
    emit questionReady(text);
}
void WrittenInterviewWindow::showOperationStatus(QString message) {
    inputStatusActive = true;
    statusBar()->showMessage(message);
}
void WrittenInterviewWindow::setShortcutStatus(QString message, bool ready) {
    shortcutStatus->setText(message);
    shortcutStatus->setProperty("shortcutError", !ready);
    shortcutStatus->style()->unpolish(shortcutStatus);
    shortcutStatus->style()->polish(shortcutStatus);
}
void WrittenInterviewWindow::ensureOnScreen() {
    if (!isVisible() || isMinimized())
        return;
    for (auto screen : QGuiApplication::screens())
        if (screen->availableGeometry().contains(frameGeometry()))
            return;
    auto screen = QGuiApplication::screenAt(frameGeometry().center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen || isMaximized())
        return;
    const auto area = screen->availableGeometry();
    resize(size().boundedTo(QSize(area.width() * 9 / 10, area.height() * 9 / 10)));
    auto bounds = frameGeometry();
    bounds.moveCenter(area.center());
    move(bounds.topLeft());
}
WindowCaptureResult WrittenInterviewWindow::setTaskbarExcluded(bool enabled) {
    const auto id = quintptr(internalWinId());
    const auto invoke = shell;
    if (!id || !invoke)
        return {false, QStringLiteral("笔试窗口尚未创建")};
    const QPointer<WrittenInterviewWindow> self(this);
    const auto result = invoke(id, enabled);
    if (self && result.ok && id == quintptr(internalWinId()))
        restoredTaskbarId = enabled ? 0 : id;
    return result;
}
void WrittenInterviewWindow::clearTaskbar() {
    const auto id = restoredTaskbarId;
    restoredTaskbarId = 0;
    const auto invoke = shell;
    if (id && invoke) {
        const auto result = invoke(id, true);
        if (!result.ok)
            qWarning("Written taskbar cleanup failed: %s", qPrintable(result.error));
    }
}
void WrittenInterviewWindow::setInput(WrittenInput *value) {
    input = value;
}
void WrittenInterviewWindow::closeEvent(QCloseEvent *event) {
    editDelay.stop();
    const QPointer<WrittenInterviewWindow> self(this);
    if (input)
        input->stop();
    if (!self)
        return;
    if (!returningNow) {
        returningNow = true;
        emit returning();
    }
    event->accept();
}
void WrittenInterviewWindow::paintEvent(QPaintEvent *event) {
    QMainWindow::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(glass ? "#73839b" : "#dce3ef"));
    painter.setBrush(WindowAppearance::surface(glass, opacity));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), isMaximized() ? 0 : 10,
                            isMaximized() ? 0 : 10);
}
void WrittenInterviewWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (windowHandle())
        windowHandle()->installEventFilter(this);
    QTimer::singleShot(0, this, &WrittenInterviewWindow::ensureOnScreen);
}
bool WrittenInterviewWindow::eventFilter(QObject *object, QEvent *event) {
    if (object == windowHandle() && event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        const QPointer<WrittenInterviewWindow> self(this);
        const QPointer<QObject> receiver(object);
        clearTaskbar();
        if (!self || !receiver)
            return true;
    }
    return QMainWindow::eventFilter(object, event);
}
bool WrittenInterviewWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
    if (type == "windows_generic_MSG" &&
        WindowCapture::isTaskbarButtonCreatedMessage(message, quintptr(internalWinId())))
        QTimer::singleShot(0, this, [this] { emit taskbarRecreated(); });
    return QMainWindow::nativeEvent(type, message, result);
}
