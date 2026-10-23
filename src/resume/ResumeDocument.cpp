#include "ResumeDocument.h"
#include "third_party/pdfium/fpdf_text.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QScopeGuard>
#include <QStringDecoder>
#include <QXmlStreamReader>
#include <QtCore/private/qzipreader_p.h>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace {
constexpr qsizetype maxCharacters = 20000;
QJsonObject fail(const QString &error) {
    return {{"error", error}};
}
QString pdfText(const QByteArray &bytes, QString &error) {
    QLibrary library(QCoreApplication::applicationDirPath() + "/resume/pdfium.dll");
    if (!library.load()) {
        error = QStringLiteral("PDF 读取组件缺失，请完整解压新版运行包；也可上传 DOCX 或 TXT");
        return {};
    }
#define PDF_FUNCTION(name) const auto name = reinterpret_cast<decltype(&::name)>(library.resolve(#name))
    PDF_FUNCTION(FPDF_InitLibrary);
    PDF_FUNCTION(FPDF_DestroyLibrary);
    PDF_FUNCTION(FPDF_LoadMemDocument64);
    PDF_FUNCTION(FPDF_GetLastError);
    PDF_FUNCTION(FPDF_CloseDocument);
    PDF_FUNCTION(FPDF_GetPageCount);
    PDF_FUNCTION(FPDF_LoadPage);
    PDF_FUNCTION(FPDF_ClosePage);
    PDF_FUNCTION(FPDFText_LoadPage);
    PDF_FUNCTION(FPDFText_ClosePage);
    PDF_FUNCTION(FPDFText_CountChars);
    PDF_FUNCTION(FPDFText_GetText);
#undef PDF_FUNCTION
    if (!FPDF_InitLibrary || !FPDF_DestroyLibrary || !FPDF_LoadMemDocument64 || !FPDF_GetLastError ||
        !FPDF_CloseDocument || !FPDF_GetPageCount || !FPDF_LoadPage || !FPDF_ClosePage ||
        !FPDFText_LoadPage || !FPDFText_ClosePage || !FPDFText_CountChars || !FPDFText_GetText) {
        error = QStringLiteral("PDF 读取组件版本不兼容，请使用完整运行包");
        return {};
    }
    FPDF_InitLibrary();
    const auto destroy = qScopeGuard([&] { FPDF_DestroyLibrary(); });
    const auto document = FPDF_LoadMemDocument64(bytes.constData(), size_t(bytes.size()), nullptr);
    if (!document) {
        error = FPDF_GetLastError() == FPDF_ERR_PASSWORD
                    ? QStringLiteral("PDF 已加密，请先另存为不加密的简历")
                    : QStringLiteral("PDF 无法读取，文件可能已损坏");
        return {};
    }
    const auto closeDocument = qScopeGuard([&] { FPDF_CloseDocument(document); });
    const auto count = FPDF_GetPageCount(document);
    if (count <= 0 || count > 30) {
        error = QStringLiteral("PDF 必须包含 1～30 页，请上传简历正文");
        return {};
    }
    QString result;
    for (int i = 0; i < count; ++i) {
        const auto page = FPDF_LoadPage(document, i);
        if (!page) {
            error = QStringLiteral("PDF 页面无法读取");
            return {};
        }
        const auto closePage = qScopeGuard([&] { FPDF_ClosePage(page); });
        const auto textPage = FPDFText_LoadPage(page);
        if (!textPage) {
            error = QStringLiteral("PDF 文字无法读取");
            return {};
        }
        const auto closeText = qScopeGuard([&] { FPDFText_ClosePage(textPage); });
        const auto characters = FPDFText_CountChars(textPage);
        if (characters < 0 || characters > maxCharacters - result.size()) {
            error = QStringLiteral("简历文字超过 20000 字，请精简后上传");
            return {};
        }
        QList<unsigned short> buffer(characters + 1);
        const auto written = FPDFText_GetText(textPage, 0, characters, buffer.data());
        if (written < 0 || written > buffer.size()) {
            error = QStringLiteral("PDF 文字提取失败");
            return {};
        }
        if (!result.isEmpty())
            result += '\n';
        result +=
            QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.constData()), qMax(0, written - 1));
    }
    if (result.trimmed().isEmpty())
        error = QStringLiteral("PDF 没有可读取的文字，可能是扫描版；请上传带文字层的 PDF、DOCX 或 TXT");
    return result;
}
QString docxText(QByteArray &bytes, QString &error) {
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QZipReader zip(&buffer);
    const auto files = zip.fileInfoList();
    if (zip.status() != QZipReader::NoError || files.size() > 4096) {
        error = QStringLiteral("DOCX 无法读取，文件可能已损坏或加密");
        return {};
    }
    QStringList parts;
    qint64 total = 0;
    for (const auto &info : files) {
        const auto &name = info.filePath;
        if (name == "word/document.xml" ||
            (name.startsWith("word/header") && name.endsWith(".xml") && name.count('/') == 1) ||
            (name.startsWith("word/footer") && name.endsWith(".xml") && name.count('/') == 1)) {
            if (!info.isFile || info.size < 0 || info.size > 2 * 1024 * 1024 ||
                (total += info.size) > 4 * 1024 * 1024) {
                error = QStringLiteral("DOCX 正文过大，请精简简历后上传");
                return {};
            }
            parts.append(name);
        }
    }
    if (!parts.contains("word/document.xml")) {
        error = QStringLiteral("文件不是有效的 DOCX 简历");
        return {};
    }
    std::stable_sort(parts.begin(), parts.end(), [](const QString &a, const QString &b) {
        auto rank = [](const QString &s) {
            return s.startsWith("word/header") ? 0 : s == "word/document.xml" ? 1 : 2;
        };
        return rank(a) == rank(b) ? a < b : rank(a) < rank(b);
    });
    QString result;
    for (const auto &part : parts) {
        const auto xmlBytes = zip.fileData(part);
        if (zip.status() != QZipReader::NoError || xmlBytes.isEmpty() || xmlBytes.size() > 2 * 1024 * 1024) {
            error = QStringLiteral("DOCX 正文无法读取");
            return {};
        }
        QXmlStreamReader xml(xmlBytes);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == u"t")
                result += xml.readElementText();
            else if (xml.isStartElement() && (xml.name() == u"tab" || xml.name() == u"br"))
                result += xml.name() == u"tab" ? '\t' : '\n';
            else if (xml.isEndElement() && (xml.name() == u"p" || xml.name() == u"tc"))
                result += xml.name() == u"tc" ? '\t' : '\n';
            if (result.size() > maxCharacters) {
                error = QStringLiteral("简历文字超过 20000 字，请精简后上传");
                return {};
            }
        }
        if (xml.hasError()) {
            error = QStringLiteral("DOCX 正文格式损坏，无法提取文字");
            return {};
        }
        result += '\n';
    }
    return result;
}
QString plainText(const QByteArray &bytes, QString &error) {
    const bool littleEndian = bytes.startsWith("\xff\xfe");
    const bool bigEndian = bytes.startsWith("\xfe\xff");
    const bool utf8Bom = bytes.startsWith("\xef\xbb\xbf");
    const bool declaredEncoding = littleEndian || bigEndian || utf8Bom;
    const auto payload = bytes.mid(littleEndian || bigEndian ? 2 : utf8Bom ? 3 : 0);
    QStringDecoder decoder(littleEndian ? QStringConverter::Utf16LE
                           : bigEndian  ? QStringConverter::Utf16BE
                                        : QStringConverter::Utf8,
                           QStringConverter::Flag::Stateless);
    QString result = decoder(payload);
    if (decoder.hasError() || !result.isValidUtf16() ||
        ((littleEndian || bigEndian) && payload.size() % 2 != 0)) {
        // A BOM declares the encoding: never reinterpret a damaged Unicode file
        // as legacy Chinese. With no BOM, strict GB18030 fallback consumes all bytes.
        if (declaredEncoding) {
            error = QStringLiteral("TXT 编码不完整或已损坏，请重新另存为 UTF-8 后上传");
            return {};
        }
#ifdef Q_OS_WIN
        const auto count = MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.constData(),
                                               int(bytes.size()), nullptr, 0);
        if (count > 0) {
            result.resize(count);
            MultiByteToWideChar(54936, MB_ERR_INVALID_CHARS, bytes.constData(), int(bytes.size()),
                                reinterpret_cast<wchar_t *>(result.data()), count);
        } else
#endif
        {
            error = QStringLiteral("TXT 编码无法识别，请另存为 UTF-8 后上传");
            return {};
        }
    }
    for (auto ch : result)
        if (ch.unicode() < 32 && ch != '\n' && ch != '\r' && ch != '\t') {
            error = QStringLiteral("文件包含二进制内容，请上传有效的文本简历");
            return {};
        }
    return result;
}
} // namespace
QJsonObject ResumeDocument::extract(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("简历无法打开，请检查文件是否存在或被占用"));
    if (file.size() > 20 * 1024 * 1024)
        return fail(QStringLiteral("简历文件最大 20 MB，请精简后上传"));
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "pdf" && suffix != "docx" && suffix != "txt")
        return fail(QStringLiteral("支持 PDF、DOCX、TXT；旧版 DOC 请先另存为 DOCX 或 PDF"));
    auto bytes = file.readAll();
    if (file.error() != QFile::NoError)
        return fail(QStringLiteral("简历文件读取失败"));
    QString error;
    QString text = suffix == "pdf"    ? pdfText(bytes, error)
                   : suffix == "docx" ? docxText(bytes, error)
                                      : plainText(bytes, error);
    text.replace("\r\n", "\n");
    text.remove(QChar(0xfeff));
    text = text.trimmed();
    if (!error.isEmpty())
        return fail(error);
    if (text.isEmpty())
        return fail(QStringLiteral("简历中没有可读取的文字"));
    if (text.size() > maxCharacters)
        return fail(QStringLiteral("简历文字超过 20000 字，请精简后上传"));
    return {
        {"fileName", QFileInfo(path).fileName()},
        {"text", text},
        {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
}
