#include "NetworkRuntime.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSslSocket>

void NetworkRuntime::initializeTls() {
    static const bool initialized = [] {
#ifdef Q_OS_WIN
        const auto directory = QCoreApplication::applicationDirPath();
        if (QFileInfo::exists(directory + "/libssl-3-x64.dll") &&
            QFileInfo::exists(directory + "/libcrypto-3-x64.dll") &&
            QSslSocket::availableBackends().contains("openssl")) {
            const auto previous = QSslSocket::activeBackend();
            // Schannel can close TLS connections through some Windows system proxies.
            // Use the shipped OpenSSL backend without changing proxy or certificate policy.
            if (QSslSocket::setActiveBackend("openssl") && !QSslSocket::supportsSsl())
                QSslSocket::setActiveBackend(previous);
        }
#endif
        return true;
    }();
    Q_UNUSED(initialized);
}
