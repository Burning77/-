#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdlib>
#include <string>
// A separate, bounded process isolates delayed clipboard rendering from the UI.
// Read only the requested sequence owned by the explicitly selected process.
int main(int argc, char **argv) {
    if (argc != 3 || !argv[1][0] || !argv[2][0])
        return 2;
    char *end = nullptr;
    const auto expected = std::strtoul(argv[1], &end, 10);
    if (!end || *end || !expected)
        return 2;
    const auto expectedProcess = std::strtoul(argv[2], &end, 10);
    if (!end || *end || !expectedProcess)
        return 2;
    const auto matchesOwner = [expectedProcess] {
        DWORD process = 0;
        const auto owner = GetClipboardOwner();
        return owner && GetWindowThreadProcessId(owner, &process) && process == expectedProcess;
    };
    if (GetClipboardSequenceNumber() != expected || !OpenClipboard(nullptr))
        return 3;
    if (!matchesOwner()) {
        CloseClipboard();
        return 3;
    }
    const auto data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return 4;
    }
    const auto bytes = GlobalSize(data);
    if (bytes < sizeof(wchar_t) || bytes > 1024 * 1024) {
        CloseClipboard();
        return 5;
    }
    const auto text = static_cast<const wchar_t *>(GlobalLock(data));
    if (!text) {
        CloseClipboard();
        return 6;
    }
    size_t length = 0, limit = bytes / sizeof(wchar_t);
    while (length < limit && text[length])
        ++length;
    if (length == limit || length > 12000) {
        GlobalUnlock(data);
        CloseClipboard();
        return 7;
    }
    const auto size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, int(length), nullptr, 0, nullptr, nullptr);
    std::string output(size > 0 ? size : 0, '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, int(length), output.data(), size, nullptr,
                            nullptr);
    GlobalUnlock(data);
    const bool unchanged = GetClipboardSequenceNumber() == expected && matchesOwner();
    CloseClipboard();
    if (!unchanged || output.empty())
        return 8;
    DWORD written = 0;
    return WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), output.data(), DWORD(output.size()), &written,
                     nullptr) &&
                   written == output.size()
               ? 0
               : 9;
}
