#include "claude_limit_source.h"

#include <winhttp.h>

#include <bit>
#include <memory>
#include <string>

namespace noitch {

namespace {

struct Request {
    HWND notify;
    uintptr_t generation;
    double now;
};

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() {
        if (value) WinHttpCloseHandle(value);
    }
};

std::wstring Environment(const wchar_t* name) {
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    return length > 0 && length < MAX_PATH ? std::wstring(buffer, length) : std::wstring();
}

// Claude Code keeps its login here on Windows and Linux; the first CLAUDE_CONFIG_DIR wins.
std::wstring CredentialsPath() {
    std::wstring folder = Environment(L"CLAUDE_CONFIG_DIR");
    if (const size_t comma = folder.find(L','); comma != std::wstring::npos) folder.resize(comma);
    if (folder.empty()) {
        const std::wstring home = Environment(L"USERPROFILE");
        if (home.empty()) return {};
        folder = home + L"\\.claude";
    }
    return folder + L"\\.credentials.json";
}

std::string ReadSmallFile(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size)) {
        const auto byteCount = std::bit_cast<LONGLONG>(size);
        if (byteCount > 0 && byteCount < (1 << 20)) {
            text.resize(static_cast<size_t>(byteCount));
            DWORD read = 0;
            if (!::ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
            text.resize(read);
        }
    }
    CloseHandle(file);
    return text;
}

ToolLimits FetchLimits(double now) {
    const std::wstring path = CredentialsPath();
    if (path.empty()) return ToolLimits::Failed(LimitProblem::SignedOut);
    std::optional<ClaudeLogin> login = ClaudeLoginFromCredentials(ReadSmallFile(path));
    if (!login) return ToolLimits::Failed(LimitProblem::SignedOut);
    if (login->expiresAt && *login->expiresAt <= now) return ToolLimits::Failed(LimitProblem::Expired);

    InternetHandle session{WinHttpOpen(L"Noitch (https://github.com/muhfauzannn/noitch)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return ToolLimits::Failed(LimitProblem::Unavailable);
    WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 10000);
    InternetHandle connection{WinHttpConnect(session.value, L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return ToolLimits::Failed(LimitProblem::Unavailable);
    InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", L"/api/oauth/usage", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return ToolLimits::Failed(LimitProblem::Unavailable);
    std::wstring headers = L"Authorization: Bearer ";
    headers.append(login->accessToken.begin(), login->accessToken.end());
    headers += L"\r\nanthropic-beta: oauth-2025-04-20\r\nAccept: application/json\r\n";
    // The token leaves this function only inside the request.
    SecureZeroMemory(login->accessToken.data(), login->accessToken.size());
    const BOOL sent = WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
    if (!sent || !WinHttpReceiveResponse(request.value, nullptr)) return ToolLimits::Failed(LimitProblem::Unavailable);
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return ToolLimits::Failed(LimitProblem::Unavailable);
    }
    if (status != 200) return ToolLimits::Failed(status == 401 ? LimitProblem::Expired : LimitProblem::Unavailable);
    std::string body;
    for (;;) {
        char chunk[4096];
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk, sizeof(chunk), &read) || read == 0) break;
        body.append(chunk, read);
    }
    const auto limits = ClaudeLimitsFromResponse(body);
    return limits ? *limits : ToolLimits::Failed(LimitProblem::Unavailable);
}

void CALLBACK Run(PTP_CALLBACK_INSTANCE, void* context) {
    const std::unique_ptr<Request> request(static_cast<Request*>(context));
    auto limits = std::make_unique<ToolLimits>(FetchLimits(request->now));
    if (PostMessageW(request->notify, ClaudeLimitSource::kMessage, request->generation, reinterpret_cast<LPARAM>(limits.get()))) limits.release();
}

}  // namespace

void ClaudeLimitSource::Fetch(HWND notify, uintptr_t generation, double now) {
    auto* request = new Request{notify, generation, now};
    if (!TrySubmitThreadpoolCallback(Run, request, nullptr)) delete request;
}

}  // namespace noitch
