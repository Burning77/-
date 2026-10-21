#include "WebSocketTransport.h"
#include <QMutexLocker>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <thread>
struct InternetHandle {
    HINTERNET h = nullptr;
    ~InternetHandle() {
        if (h)
            WinHttpCloseHandle(h);
    }
};
bool WebSocketTransport::send(const QByteArray &bytes) {
    QMutexLocker lock(&mutex);
    auto ws = socket.load();
    if (!ws || stopping)
        return false;
    if (queuedBytes + bytes.size() > 2 * 1024 * 1024)
        return false;
    outgoing.enqueue(bytes);
    queuedBytes += bytes.size();
    outgoingReady.wakeOne();
    return true;
}
void WebSocketTransport::stop() {
    stopping = true;
    QMutexLocker lock(&mutex);
    outgoingReady.wakeAll();
    if (auto ws = socket.exchange(nullptr))
        WinHttpCloseHandle(ws);
    if (auto r = request.exchange(nullptr))
        WinHttpCloseHandle(r);
}
void WebSocketTransport::run() {
    auto fail = [this](QString text) {
        if (!stopping)
            emit transportError(text);
    };
    InternetHandle session{WinHttpOpen(L"InterviewAssistant/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) {
        fail(QStringLiteral("无法初始化 WebSocket 网络"));
        return;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 10000, 60000);
    auto host = url.host().toStdWString();
    InternetHandle connection{WinHttpConnect(session.h, host.c_str(),
                                             INTERNET_PORT(url.port(url.scheme() == "wss" ? 443 : 80)), 0)};
    if (!connection.h) {
        fail(QStringLiteral("无法连接转写服务"));
        return;
    }
    auto path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
        path = "/";
    if (!url.query().isEmpty())
        path += "?" + url.query(QUrl::FullyEncoded);
    auto wide = path.toStdWString();
    auto req =
        WinHttpOpenRequest(connection.h, L"GET", wide.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, url.scheme() == "wss" ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        fail(QStringLiteral("无法创建 WebSocket 请求"));
        return;
    }
    {
        QMutexLocker lock(&mutex);
        if (stopping) {
            WinHttpCloseHandle(req);
            return;
        }
        request = req;
    }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
    auto header = (QString("Authorization: Bearer ") + key + "\r\n").toStdWString();
    if (!WinHttpAddRequestHeaders(req, header.c_str(), DWORD(-1), WINHTTP_ADDREQ_FLAG_ADD) ||
        !WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        fail(QStringLiteral("WebSocket 连接失败（错误 %1）").arg(GetLastError()));
        stop();
        return;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 101) {
        fail(QStringLiteral("转写连接被拒绝（HTTP %1），请检查 API 权限与地址").arg(status));
        stop();
        return;
    }
    auto ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (!ws) {
        fail(QStringLiteral("WebSocket 协议升级失败"));
        stop();
        return;
    }
    {
        QMutexLocker lock(&mutex);
        if (stopping) {
            WinHttpCloseHandle(ws);
            return;
        }
        socket = ws;
        if (auto r = request.exchange(nullptr))
            WinHttpCloseHandle(r);
    }
    std::thread sender([this, ws] {
        while (!stopping) {
            QByteArray data;
            {
                QMutexLocker lock(&mutex);
                while (outgoing.isEmpty() && !stopping)
                    outgoingReady.wait(&mutex);
                if (stopping)
                    break;
                data = outgoing.dequeue();
                queuedBytes -= data.size();
            }
            auto error = WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, data.data(),
                                              DWORD(data.size()));
            if (error != NO_ERROR) {
                if (!stopping)
                    emit transportError(QStringLiteral("转写音频发送失败（错误 %1）").arg(error));
                stop();
                break;
            }
        }
    });
    emit connected();
    QByteArray full;
    char buf[16384];
    while (!stopping) {
        DWORD received = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        auto error = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &received, &type);
        if (error != NO_ERROR) {
            fail(QStringLiteral("转写网络连接中断（错误 %1）").arg(error));
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            fail(QStringLiteral("转写服务关闭了连接"));
            break;
        }
        full.append(buf, int(received));
        if (full.size() > 4 * 1024 * 1024) {
            fail(QStringLiteral("转写事件过大"));
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
            emit message(full);
            full.clear();
        }
    }
    stop();
    sender.join();
}
