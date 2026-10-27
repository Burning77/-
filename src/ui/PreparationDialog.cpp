#include "PreparationDialog.h"
#include "ResumeUploadWidget.h"
#include "app/AppSettings.h"
#include "preparation/Preparation.h"
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
PreparationDialog::PreparationDialog(QString path, QJsonObject initial, bool locked, QWidget *parent)
    : QDialog(parent), root(path), data(initial), client(this), readOnly(locked) {
    setWindowTitle(locked ? QStringLiteral("本次面试准备 · 只读快照")
                          : QStringLiteral("面试准备 · 公司、岗位和专业术语"));
    if (auto screen = QGuiApplication::primaryScreen())
        resize(qMin(960, screen->availableGeometry().width() * 85 / 100),
               qMin(720, screen->availableGeometry().height() * 85 / 100));
    auto layout = new QVBoxLayout(this);
    auto help = new QLabel(
        locked ? QStringLiteral("以下为开始本次面试时保存的准备资料；编辑下次准备请结束录音后操作。")
               : QStringLiteral("填写公司、岗位和职位描述，上传简历后结合分析。联网阶段只查询公司名称；"
                                "随后将来源、职位描述及简历发给当前 AI 分析。术语表可手动补充。"));
    help->setWordWrap(true);
    layout->addWidget(help);
    tabs = new QTabWidget;
    layout->addWidget(tabs, 1);
    auto page = new QWidget;
    auto form = new QFormLayout(page);
    company = new QLineEdit(data["company"].toString());
    company->setObjectName("prepCompany");
    company->setMaxLength(200);
    role = new QLineEdit(data["role"].toString());
    role->setObjectName("prepRole");
    role->setMaxLength(300);
    job = new QPlainTextEdit(data["jobDescription"].toString());
    job->setObjectName("prepJobDescription");
    job->setPlaceholderText(QStringLiteral("粘贴招聘要求、技术栈、职责；尽量提供原始职位描述"));
    resume = new ResumeUploadWidget;
    resume->setDocument(data["resume"].toObject(), data["experience"].toString());
    form->addRow(QStringLiteral("公司"), company);
    onlineCompany = new QCheckBox(QStringLiteral("联网核实公司信息（ChatGPT / OpenAI）"));
    onlineCompany->setObjectName("verifyCompanyOnline");
    onlineCompany->setChecked(data.value("verifyCompanyOnline").toBool(!locked));
    onlineCompany->setEnabled(!locked);
    form->addRow(QString(), onlineCompany);
    form->addRow(QStringLiteral("岗位"), role);
    form->addRow(QStringLiteral("职位描述（最多12000字）"), job);
    form->addRow(QStringLiteral("个人简历"), resume);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    scroll->setFrameShape(QFrame::NoFrame);
    tabs->addTab(scroll, QStringLiteral("公司与岗位"));
    analysis = new QTextBrowser;
    analysis->setObjectName("prepAnalysis");
    analysis->setOpenExternalLinks(false);
    analysis->setOpenLinks(false);
    auto analysisPage = new QWidget;
    auto analysisLayout = new QVBoxLayout(analysisPage);
    analysisLayout->setContentsMargins(0, 0, 0, 0);
    analysisNotice = new QLabel;
    analysisNotice->setObjectName("prepAnalysisStatus");
    analysisNotice->setTextFormat(Qt::PlainText);
    analysisNotice->setWordWrap(true);
    analysisLayout->addWidget(analysisNotice);
    analysisLayout->addWidget(analysis, 1);
    tabs->addTab(analysisPage, QStringLiteral("面试前分析"));
    connect(analysis, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        const auto snapshot = analysisResearchSnapshot();
        for (auto source : snapshot["companyResearch"].toObject()["sources"].toArray())
            if (QUrl(source.toObject()["url"].toString()) == url) {
                QDesktopServices::openUrl(url);
                return;
            }
    });
    auto termPage = new QWidget;
    auto termLayout = new QVBoxLayout(termPage);
    auto termHelp = new QLabel(QStringLiteral("标准术语 / 读法或误识别别名 / "
                                              "含义。别名用逗号分隔。只填写明确的对应关系，模糊同音词可能误纠"
                                              "正。原始转写会保留；本地模型并未重新训练。"));
    termHelp->setWordWrap(true);
    termLayout->addWidget(termHelp);
    terms = new QTableWidget(0, 3);
    terms->setObjectName("prepTerms");
    terms->setHorizontalHeaderLabels(
        {QStringLiteral("标准术语"), QStringLiteral("读法、别名"), QStringLiteral("中文含义 / 岗位用途")});
    terms->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    termLayout->addWidget(terms);
    auto termButtons = new QHBoxLayout;
    addTerm = new QPushButton(QStringLiteral("添加术语"));
    removeTerm = new QPushButton(QStringLiteral("删除所选术语"));
    termButtons->addWidget(addTerm);
    termButtons->addWidget(removeTerm);
    termButtons->addStretch();
    termLayout->addLayout(termButtons);
    tabs->addTab(termPage, QStringLiteral("专业术语表"));
    showTerms();
    status = new QLabel;
    status->setObjectName("preparationStatus");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    auto controls = new QHBoxLayout;
    analyze = new QPushButton(QStringLiteral("AI 分析公司与岗位"));
    analyze->setObjectName("analyzePreparation");
    stop = new QPushButton(QStringLiteral("停止分析"));
    saveButton = new QPushButton(QStringLiteral("保存准备信息"));
    saveButton->setObjectName("savePreparation");
    auto close = new QPushButton(QStringLiteral("关闭"));
    controls->addWidget(analyze);
    controls->addWidget(stop);
    controls->addStretch();
    controls->addWidget(saveButton);
    controls->addWidget(close);
    layout->addLayout(controls);
    company->setReadOnly(locked);
    role->setReadOnly(locked);
    job->setReadOnly(locked);
    resume->setReadOnly(locked);
    if (locked) {
        terms->setEditTriggers(QAbstractItemView::NoEditTriggers);
        analyze->hide();
        stop->hide();
        saveButton->hide();
        addTerm->hide();
        removeTerm->hide();
    }
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(addTerm, &QPushButton::clicked, this, [this] {
        if (terms->rowCount() < 60)
            terms->insertRow(terms->rowCount());
    });
    connect(removeTerm, &QPushButton::clicked, this, [this] {
        if (terms->currentRow() >= 0) {
            terms->removeRow(terms->currentRow());
            glossaryEdited = true;
        }
    });
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (saveDraft())
            accept();
    });
    connect(analyze, &QPushButton::clicked, this, &PreparationDialog::runAnalysis);
    connect(stop, &QPushButton::clicked, this, [this] {
        cancelAnalysis();
        status->setText(QStringLiteral("分析已停止，准备资料已保留"));
    });
    connect(&client, &TextClient::textDelta, this, [this](QString id, QString text) {
        if (id != activeId)
            return;
        buffer += text;
        if (buffer.size() > 100000) {
            cancelAnalysis();
            failAnalysis(QStringLiteral("分析响应过大，已停止"));
        }
    });
    connect(&client, &TextClient::failed, this, [this](QString id, QString error) {
        if (id != activeId)
            return;
        failAnalysis(error);
    });
    connect(&client, &TextClient::webSources, this, [this](QString id, QJsonArray sources) {
        if (id == activeId && researching)
            researchSources = sources;
    });
    connect(&client, &TextClient::finished, this, [this](QString id) {
        if (id != activeId)
            return;
        activeId.clear();
        QString error;
        if (researching) {
            researching = false;
            requestInput["companyResearch"] =
                QJsonObject{{"status", "completed"},      {"company", requestInput["company"]},
                            {"text", buffer.left(16000)}, {"sources", researchSources},
                            {"checkedAt", utcNow()},      {"provider", activeConfig.provider},
                            {"model", activeConfig.model}};
            if (!Preparation::hasCompanyResearch(requestInput)) {
                failAnalysis(QStringLiteral("未取得有效的公司联网资料和来源，本次分析未更新"));
                return;
            }
            beginPreparation();
            return;
        }
        auto result = Preparation::parseResult(buffer, &error);
        if (!error.isEmpty()) {
            failAnalysis(error);
            return;
        }
        data = requestInput;
        data["analysis"] = result["analysis"];
        data["terms"] = result["terms"];
        data["analysisBasis"] = Preparation::fingerprint(data);
        data["termsBasis"] = Preparation::fingerprint(data);
        data["analyzedAt"] = utcNow();
        if (Preparation::hasCompanyResearch(data))
            data["analysisResearch"] = data["companyResearch"];
        else
            data.remove("analysisResearch");
        analysisError.clear();
        setBusy(false);
        refreshAnalysisView();
        showTerms();
        if (!Preparation::save(root, data, &error)) {
            failAnalysis(QStringLiteral("分析已生成，但保存失败：") + error, true);
            return;
        }
        tabs->setCurrentIndex(1);
        status->setText(QStringLiteral("分析已完成并保存；请检查术语表，可修改后再次保存"));
    });
    connect(terms, &QTableWidget::itemChanged, this, [this] { glossaryEdited = true; });
    auto stale = [this] {
        refreshAnalysisView();
        auto current = gather();
        if (activeId.isEmpty() && analysisError.isEmpty() && !data["analysis"].toString().isEmpty() &&
            data["analysisBasis"] != Preparation::fingerprint(current))
            status->setText(QStringLiteral("资料已修改，请重新分析；旧分析和术语不会自动用于新资料"));
    };
    connect(company, &QLineEdit::textChanged, this, stale);
    connect(role, &QLineEdit::textChanged, this, stale);
    connect(job, &QPlainTextEdit::textChanged, this, stale);
    connect(resume, &ResumeUploadWidget::documentChanged, this, stale);
    connect(onlineCompany, &QCheckBox::toggled, this, stale);
    connect(resume, &ResumeUploadWidget::busyChanged, this, [this](bool busy) { setBusy(busy); });
    setBusy(false);
    stale();
}
QJsonObject PreparationDialog::gather() const {
    auto value = data;
    // Preserve references associated with the retained report while its inputs change.
    if (!data["analysis"].toString().isEmpty() && data["analysisBasis"] == Preparation::fingerprint(data) &&
        Preparation::hasCompanyResearch(data))
        value["analysisResearch"] = data["companyResearch"];
    value["company"] = company->text().trimmed();
    value["role"] = role->text().trimmed();
    value["jobDescription"] = job->toPlainText();
    value["resume"] = resume->document();
    value["experience"] = QString();
    value["verifyCompanyOnline"] = onlineCompany->isChecked();
    if (!onlineCompany->isChecked() || value["companyResearch"].toObject()["company"] != value["company"])
        value.remove("companyResearch");
    QJsonArray values;
    for (int row = 0; row < terms->rowCount(); row++) {
        auto cell = [&](int column) {
            auto item = terms->item(row, column);
            return item ? item->text().trimmed() : QString();
        };
        auto term = cell(0);
        if (term.isEmpty())
            continue;
        QJsonArray aliases;
        for (auto alias : cell(1).split(QRegularExpression("[,，;；\n]"), Qt::SkipEmptyParts))
            aliases.append(alias.trimmed());
        values.append(QJsonObject{{"term", term}, {"aliases", aliases}, {"meaning", cell(2)}});
    }
    value["terms"] = values;
    return value;
}
void PreparationDialog::showTerms() {
    QSignalBlocker block(terms);
    glossaryEdited = false;
    terms->setRowCount(0);
    for (auto value : data["terms"].toArray()) {
        if (terms->rowCount() >= 60)
            break;
        auto term = value.toObject();
        auto row = terms->rowCount();
        terms->insertRow(row);
        QStringList aliases;
        for (auto alias : term["aliases"].toArray())
            aliases.append(alias.toString());
        terms->setItem(row, 0, new QTableWidgetItem(term["term"].toString()));
        terms->setItem(row, 1, new QTableWidgetItem(aliases.join(QStringLiteral("，"))));
        terms->setItem(row, 2, new QTableWidgetItem(term["meaning"].toString()));
    }
}
void PreparationDialog::setBusy(bool busy) {
    onlineCompany->setEnabled(!busy && !readOnly);
    for (auto widget :
         {static_cast<QWidget *>(company), static_cast<QWidget *>(role), static_cast<QWidget *>(job),
          static_cast<QWidget *>(resume), static_cast<QWidget *>(terms)})
        widget->setEnabled(!busy);
    analyze->setEnabled(!busy && !readOnly);
    saveButton->setEnabled(!busy && !readOnly);
    stop->setEnabled(!activeId.isEmpty());
    addTerm->setEnabled(!busy && !readOnly);
    removeTerm->setEnabled(!busy && !readOnly);
}
void PreparationDialog::cancelAnalysis() {
    auto id = activeId;
    activeId.clear();
    researching = false;
    if (!id.isEmpty())
        client.cancel(id);
    setBusy(false);
    if (!id.isEmpty()) {
        analysisError = QStringLiteral("分析已停止，以下保留上次结果");
        refreshAnalysisView();
    }
}
bool PreparationDialog::saveDraft() {
    if (readOnly || resume->busy())
        return false;
    auto value = gather();
    if (job->toPlainText().size() > 12000 || Preparation::candidateText(value).size() > 20000 ||
        terms->rowCount() > 60) {
        status->setText(QStringLiteral("资料过长：职位描述最多12000字，简历文字最多20000字，术语最多60条"));
        return false;
    }
    for (auto term : value["terms"].toArray()) {
        auto t = term.toObject();
        if (t["term"].toString().size() > 80 || t["meaning"].toString().size() > 800 ||
            t["aliases"].toArray().size() > 8) {
            status->setText(QStringLiteral("术语最多80字、含义最多800字、别名最多8个"));
            return false;
        }
    }
    if (glossaryEdited || value["termsBasis"] == Preparation::fingerprint(value))
        value["termsBasis"] = Preparation::fingerprint(value);
    QString error;
    if (!Preparation::save(root, value, &error)) {
        status->setText(error);
        return false;
    }
    data = value;
    generatedUnsaved = false;
    return true;
}
void PreparationDialog::runAnalysis() {
    if (readOnly || !activeId.isEmpty() || resume->busy())
        return;
    if (company->text().trimmed().isEmpty() || role->text().trimmed().isEmpty()) {
        status->setText(QStringLiteral("请先填写公司和岗位"));
        return;
    }
    if (!saveDraft())
        return;
    requestInput = data;
    buffer.clear();
    analysisError.clear();
    researchSources = {};
    activeConfig = AppSettings().textConfig();
    if (!onlineCompany->isChecked()) {
        beginPreparation();
        return;
    }
    if (activeConfig.provider != "ChatGPT" && activeConfig.provider != "OpenAI") {
        failAnalysis(
            QStringLiteral("当前版本联网核验支持 ChatGPT / OpenAI；请切换服务，或取消联网仅分析填写资料"));
        return;
    }
    auto request = Preparation::companyResearchRequest(requestInput["company"].toString(), activeConfig);
    researching = true;
    activeId = request.id;
    setBusy(true);
    tabs->setCurrentIndex(1);
    refreshAnalysisView();
    status->setText(QStringLiteral("正在联网核实公司公开信息…"));
    client.start(request);
}
void PreparationDialog::beginPreparation() {
    buffer.clear();
    researching = false;
    auto request = Preparation::request(requestInput, activeConfig);
    requestInput["analysisProvider"] = request.config.provider;
    requestInput["analysisModel"] = request.config.model;
    requestInput["analysisEffort"] =
        request.config.provider == "ChatGPT" ? request.config.reasoningEffort : QString();
    activeId = request.id;
    setBusy(true);
    tabs->setCurrentIndex(1);
    refreshAnalysisView();
    status->setText(
        QStringLiteral("正在使用 %1 / %2 分析…").arg(request.config.provider, request.config.model));
    client.start(request);
}
void PreparationDialog::failAnalysis(const QString &error, bool generated) {
    activeId.clear();
    researching = false;
    analysisError = error;
    generatedUnsaved = generated;
    setBusy(false);
    refreshAnalysisView();
    status->setText(error);
}
void PreparationDialog::refreshAnalysisView() {
    QString text = data["analysis"].toString();
    QString notice;
    const bool stale = data["analysisBasis"] != Preparation::fingerprint(gather());
    if (!activeId.isEmpty()) {
        notice = researching ? QStringLiteral("正在联网核实公司（仅查询公司名称）…")
                             : QStringLiteral("正在结合公司资料、职位描述与简历生成新分析…");
        text = QStringLiteral("分析完成后会在这里显示本次结果。上次结果已保留；停止或失败不会覆盖它。");
    } else if (generatedUnsaved)
        notice = QStringLiteral("本次分析已生成但未保存：%1。请检查保存位置后点击“保存准备信息”重试。")
                     .arg(analysisError);
    else if (!analysisError.isEmpty())
        notice = QStringLiteral("本次分析失败或已停止：%1。以下为上次保留的结果，并非本次新分析。")
                     .arg(analysisError);
    else if (text.isEmpty())
        notice = QStringLiteral("尚未生成分析，请点击“AI 分析公司与岗位”。");
    else if (stale)
        notice = QStringLiteral("上次分析已过期：公司、职位描述、简历或联网资料已改变，请重新分析。");
    else
        notice = Preparation::hasCompanyResearch(data)
                     ? QStringLiteral("已完成：结合公司联网来源、职位描述与简历的分析。")
                     : QStringLiteral("已完成：仅根据填写资料与简历分析，未联网核实公司。");
    if (activeId.isEmpty())
        text += Preparation::researchSourcesMarkdown(analysisResearchSnapshot());
    analysisNotice->setText(notice);
    if (text != displayedAnalysis) {
        analysis->setMarkdown(text);
        displayedAnalysis = text;
    }
}
QJsonObject PreparationDialog::analysisResearchSnapshot() const {
    auto research = data["analysisResearch"].toObject();
    if (research.isEmpty() && data["analysisBasis"] == Preparation::fingerprint(data) &&
        Preparation::hasCompanyResearch(data))
        research = data["companyResearch"].toObject();
    const QJsonObject snapshot{
        {"company", research["company"]}, {"companyResearch", research}, {"verifyCompanyOnline", true}};
    return Preparation::hasCompanyResearch(snapshot) ? snapshot : QJsonObject{};
}
void PreparationDialog::done(int result) {
    cancelAnalysis();
    QDialog::done(result);
}
