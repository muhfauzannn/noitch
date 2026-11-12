#include "moodle_source.h"

#include <wincred.h>
#include <winhttp.h>

#include <memory>

#include "features/lyrics/lyrics.h"
#include "tasks_settings.h"

namespace noitch {

namespace {

struct Request {
    HWND notify;
    uintptr_t generation;
    MoodleSource::Kind kind;
    std::string site;
    std::string username;
    std::string secret;  // The password, or a pasted key.
    double now;
};

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() {
        if (value) WinHttpCloseHandle(value);
    }
};

std::wstring Wide(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

void Wipe(std::string& text) {
    SecureZeroMemory(text.data(), text.size());
    text.clear();
}

// POSTs a form to `path` on the site. Only HTTPS: the site was normalized when it was saved.
MoodleProblem Post(const std::string& site, const wchar_t* path, std::string& body, std::string* answer) {
    const std::string host = TasksSiteHost(site);
    const size_t slash = site.find('/', 8);
    std::wstring fullPath = slash == std::string::npos ? std::wstring(L"/") : Wide(site.substr(slash)) + L"/";
    fullPath += path;
    InternetHandle session{WinHttpOpen(L"Noitch (https://github.com/muhfauzannn/noitch)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return MoodleProblem::Unavailable;
    WinHttpSetTimeouts(session.value, 15000, 15000, 15000, 15000);
    InternetHandle connection{WinHttpConnect(session.value, Wide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return MoodleProblem::Unavailable;
    InternetHandle request{WinHttpOpenRequest(connection.value, L"POST", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return MoodleProblem::Unavailable;
    constexpr wchar_t kHeaders[] = L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n";
    const BOOL sent = WinHttpSendRequest(request.value, kHeaders, static_cast<DWORD>(-1L), body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    // The body may hold the password or the key; it leaves this function only inside the request.
    Wipe(body);
    if (!sent || !WinHttpReceiveResponse(request.value, nullptr)) return MoodleProblem::Unavailable;
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return MoodleProblem::Unavailable;
    }
    if (status != 200) return status == 503 ? MoodleProblem::Maintenance : MoodleProblem::Unavailable;
    answer->clear();
    for (;;) {
        char chunk[4096];
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk, sizeof(chunk), &read) || read == 0) break;
        answer->append(chunk, read);
        // A task list is tens of kilobytes; anything far larger isn't one.
        if (answer->size() > (4u << 20)) return MoodleProblem::Unavailable;
    }
    return MoodleProblem::None;
}

std::string Field(const char* name, const std::string& value) { return std::string(name) + "=" + PercentEncode(value); }

MoodleProblem Call(const std::string& site, const std::string& token, const char* function, const std::string& arguments, std::string* answer) {
    std::string body = Field("wstoken", token) + "&" + Field("wsfunction", function) + "&moodlewsrestformat=json" + arguments;
    return Post(site, L"webservice/rest/server.php", body, answer);
}

void CheckKey(MoodleSource::Result& result, const std::string& site, std::string& token) {
    std::string answer;
    result.problem = Call(site, token, "core_webservice_get_site_info", "", &answer);
    if (result.problem == MoodleProblem::None) result.problem = MoodleSiteInfoFrom(answer, &result.info);
    if (result.problem == MoodleProblem::None) result.token = token;
    Wipe(token);
}

void CALLBACK Run(PTP_CALLBACK_INSTANCE, void* context) {
    const std::unique_ptr<Request> request(static_cast<Request*>(context));
    auto result = std::make_unique<MoodleSource::Result>();
    result->kind = request->kind;
    std::string answer;
    switch (request->kind) {
        case MoodleSource::Kind::SignIn: {
            std::string body = Field("username", request->username) + "&" + Field("password", request->secret) + "&service=moodle_mobile_app";
            Wipe(request->secret);
            result->problem = Post(request->site, L"login/token.php", body, &answer);
            std::string token;
            if (result->problem == MoodleProblem::None) result->problem = MoodleTokenFrom(answer, &token, &result->message);
            if (result->problem == MoodleProblem::None) CheckKey(*result, request->site, token);
            break;
        }
        case MoodleSource::Kind::UseKey:
            CheckKey(*result, request->site, request->secret);
            break;
        case MoodleSource::Kind::Tasks: {
            std::string token = task_token::Read(request->site);
            if (token.empty()) {
                result->problem = MoodleProblem::SignedOut;
                break;
            }
            const std::string arguments = "&timesortfrom=" + std::to_string(static_cast<int64_t>(request->now - TaskRule::kOverdueKept)) +
                                          "&limitnum=50&limittononsuspendedevents=1";
            result->problem = Call(request->site, token, "core_calendar_get_action_events_by_timesort", arguments, &answer);
            Wipe(token);
            if (result->problem == MoodleProblem::None) result->problem = MoodleTasksFrom(answer, &result->tasks);
            break;
        }
    }
    if (PostMessageW(request->notify, MoodleSource::kMessage, request->generation, reinterpret_cast<LPARAM>(result.get()))) result.release();
}

void Submit(Request* request) {
    if (!TrySubmitThreadpoolCallback(Run, request, nullptr)) {
        Wipe(request->secret);
        delete request;
    }
}

std::wstring Target(const std::string& site) { return L"Noitch/Moodle/" + Wide(TasksSiteHost(site)); }

}  // namespace

void MoodleSource::SignIn(HWND notify, uintptr_t generation, const std::string& site, const std::string& username, std::string password) {
    Submit(new Request{notify, generation, Kind::SignIn, site, username, std::move(password), 0});
}

void MoodleSource::UseKey(HWND notify, uintptr_t generation, const std::string& site, std::string token) {
    Submit(new Request{notify, generation, Kind::UseKey, site, {}, std::move(token), 0});
}

void MoodleSource::Tasks(HWND notify, uintptr_t generation, const std::string& site, double now) {
    Submit(new Request{notify, generation, Kind::Tasks, site, {}, {}, now});
}

namespace task_token {

bool Save(const std::string& site, const std::string& token) {
    std::wstring target = Target(site);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target.data();
    credential.CredentialBlobSize = static_cast<DWORD>(token.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(token.data()));
    // This user on this PC; Windows encrypts it with the user's login.
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    wchar_t user[] = L"Noitch";
    credential.UserName = user;
    return CredWriteW(&credential, 0) != FALSE;
}

std::string Read(const std::string& site) {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(Target(site).c_str(), CRED_TYPE_GENERIC, 0, &credential)) return {};
    std::string token(reinterpret_cast<const char*>(credential->CredentialBlob), credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
    return token;
}

void Delete(const std::string& site) { CredDeleteW(Target(site).c_str(), CRED_TYPE_GENERIC, 0); }

}  // namespace task_token

}  // namespace noitch
