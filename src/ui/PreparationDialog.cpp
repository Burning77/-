#include "PreparationDialog.h"
#include "app/AppSettings.h"
#include "preparation/Preparation.h"
#include <QApplication>
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
               : QStringLiteral("填写公司、岗位和职位描述，使用当前所选 AI "
                                "分析；仅分析填写资料，未联网查询公司。术语表可手动补充。"));
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
    experience = new QPlainTextEdit(data["experience"].toString());
    experience->setObjectName("prepExperience");
    experience->setPlaceholderText(QStringLiteral("可选：真实项目经历，帮助分析准备方向"));
    form->addRow(QStringLiteral("公司"), company);
    form->addRow(QStringLiteral("岗位"), role);
    form->addRow(QStringLiteral("职位描述（最多12000字）"), job);
    form->addRow(QStringLiteral("真实经历（最多4000字）"), experience);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    scroll->setFrameShape(QFrame::NoFrame);
    tabs->addTab(scroll, QStringLiteral("公司与岗位"));
    analysis = new QTextBrowser;
    analysis->setObjectName("prepAnalysis");
    analysis->setOpenExternalLinks(false);
    analysis->setOpenLinks(false);
    analysis->setMarkdown(data["analysis"].toString());
    tabs->addTab(analysis, QStringLiteral("面试前分析"));
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
    experience->setReadOnly(locked);
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
            status->setText(QStringLiteral("分析响应过大，已停止"));
        }
    });
    connect(&client, &TextClient::failed, this, [this](QString id, QString error) {
        if (id != activeId)
            return;
        activeId.clear();
        setBusy(false);
        status->setText(error);
    });
    connect(&client, &TextClient::finished, this, [this](QString id) {
        if (id != activeId)
            return;
        activeId.clear();
        setBusy(false);
        QString error;
        auto result = Preparation::parseResult(buffer, &error);
        if (!error.isEmpty()) {
            status->setText(error);
            return;
        }
        data = requestInput;
        data["analysis"] = result["analysis"];
        data["terms"] = result["terms"];
        data["analysisBasis"] = Preparation::fingerprint(data);
        data["termsBasis"] = Preparation::fingerprint(data);
        data["analyzedAt"] = utcNow();
        analysis->setMarkdown(data["analysis"].toString());
        showTerms();
        if (!Preparation::save(root, data, &error)) {
            status->setText(error);
            return;
        }
        tabs->setCurrentIndex(1);
        status->setText(QStringLiteral("分析已完成并保存；请检查术语表，可修改后再次保存"));
    });
    connect(terms, &QTableWidget::itemChanged, this, [this] { glossaryEdited = true; });
    auto stale = [this] {
        auto current = gather();
        if (!data["analysis"].toString().isEmpty() &&
            data["analysisBasis"] != Preparation::fingerprint(current))
            status->setText(QStringLiteral("资料已修改，请重新分析；旧分析和术语不会自动用于新资料"));
    };
    connect(company, &QLineEdit::textChanged, this, stale);
    connect(role, &QLineEdit::textChanged, this, stale);
    connect(job, &QPlainTextEdit::textChanged, this, stale);
    connect(experience, &QPlainTextEdit::textChanged, this, stale);
    setBusy(false);
    stale();
}
QJsonObject PreparationDialog::gather() const {
    auto value = data;
    value["company"] = company->text().trimmed();
    value["role"] = role->text().trimmed();
    value["jobDescription"] = job->toPlainText();
    value["experience"] = experience->toPlainText();
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
    for (auto widget :
         {static_cast<QWidget *>(company), static_cast<QWidget *>(role), static_cast<QWidget *>(job),
          static_cast<QWidget *>(experience), static_cast<QWidget *>(terms)})
        widget->setEnabled(!busy);
    analyze->setEnabled(!busy && !readOnly);
    saveButton->setEnabled(!busy && !readOnly);
    stop->setEnabled(busy);
    addTerm->setEnabled(!busy && !readOnly);
    removeTerm->setEnabled(!busy && !readOnly);
}
void PreparationDialog::cancelAnalysis() {
    auto id = activeId;
    activeId.clear();
    if (!id.isEmpty())
        client.cancel(id);
    setBusy(false);
}
bool PreparationDialog::saveDraft() {
    if (readOnly)
        return false;
    auto value = gather();
    if (job->toPlainText().size() > 12000 || experience->toPlainText().size() > 4000 ||
        terms->rowCount() > 60) {
        status->setText(QStringLiteral("资料过长：职位描述最多12000字，经历最多4000字，术语最多60条"));
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
    return true;
}
void PreparationDialog::runAnalysis() {
    if (readOnly || !activeId.isEmpty())
        return;
    if (company->text().trimmed().isEmpty() || role->text().trimmed().isEmpty()) {
        status->setText(QStringLiteral("请先填写公司和岗位"));
        return;
    }
    if (!saveDraft())
        return;
    requestInput = data;
    buffer.clear();
    auto request = Preparation::request(requestInput, AppSettings().textConfig());
    requestInput["analysisProvider"] = request.config.provider;
    requestInput["analysisModel"] = request.config.model;
    activeId = request.id;
    setBusy(true);
    status->setText(
        QStringLiteral("正在使用 %1 / %2 分析…").arg(request.config.provider, request.config.model));
    client.start(request);
}
void PreparationDialog::done(int result) {
    cancelAnalysis();
    QDialog::done(result);
}
