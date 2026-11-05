#include "lyrics_source.h"

#include <shlobj.h>
#include <winhttp.h>

#include <cstdio>
#include <cwchar>
#include <memory>

namespace noitch {

namespace {

struct Request {
    HWND notify;
    uintptr_t generation;
    std::string title;
    std::string artist;
    std::string album;
    double duration;
};

std::wstring Widen(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() {
        if (value) WinHttpCloseHandle(value);
    }
};

// An empty result means anything from no connection to a 404. All of it means "no lyrics".
std::string Get(HINTERNET connection, const std::string& pathAndQuery) {
    InternetHandle request{WinHttpOpenRequest(connection, L"GET", Widen(pathAndQuery).c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!request.value) return {};
    if (!WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return {};
    if (!WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) return {};
    std::string body;
    for (;;) {
        char chunk[8192];
        DWORD read = 0;
        if (!WinHttpReadData(request.value, chunk, sizeof(chunk), &read) || read == 0) return body;
        body.append(chunk, read);
    }
}

std::wstring CachePath(const Request& request) {
    wchar_t* base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) return {};
    std::wstring folder = std::wstring(base) + L"\\Noitch";
    CoTaskMemFree(base);
    CreateDirectoryW(folder.c_str(), nullptr);
    folder += L"\\Lyrics";
    CreateDirectoryW(folder.c_str(), nullptr);
    wchar_t name[32];
    swprintf(name, 32, L"\\%016llx.json", static_cast<unsigned long long>(LyricsCacheKey(request.title, request.artist)));
    return folder + name;
}

std::string ReadFileText(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    std::string text;
    char chunk[8192];
    DWORD read = 0;
    while (ReadFile(file, chunk, sizeof(chunk), &read, nullptr) && read > 0) text.append(chunk, read);
    CloseHandle(file);
    return text;
}

void WriteFileText(const std::wstring& path, const std::string& text) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

Lyrics Find(const Request& request) {
    const std::wstring cache = CachePath(request);
    if (!cache.empty()) {
        const std::string cached = ReadFileText(cache);
        if (const auto record = BestLyricsRecord(ParseLyricsRecords(cached), request.duration)) return record->ToLyrics();
    }

    InternetHandle session{WinHttpOpen(L"Noitch (https://github.com/muhfauzannn/noitch)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return {};
    WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 10000);
    InternetHandle connection{WinHttpConnect(session.value, L"lrclib.net", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return {};

    const std::string query = "?track_name=" + PercentEncode(request.title) + "&artist_name=" + PercentEncode(request.artist);
    std::string body;
    std::optional<LyricsRecord> record;
    if (!request.album.empty() && request.duration > 0) {
        char duration[32];
        std::snprintf(duration, sizeof(duration), "%d", static_cast<int>(request.duration + 0.5));
        body = Get(connection.value, "/api/get" + query + "&album_name=" + PercentEncode(request.album) + "&duration=" + duration);
        record = BestLyricsRecord(ParseLyricsRecords(body), request.duration);
    }
    if (!record || record->syncedLyrics.empty()) {
        body = Get(connection.value, "/api/search" + query);
        record = BestLyricsRecord(ParseLyricsRecords(body), request.duration);
    }
    if (!record) return {};
    // Misses aren't stored, so a track that gets lyrics later is found next time.
    if (!cache.empty()) WriteFileText(cache, body);
    return record->ToLyrics();
}

void CALLBACK Run(PTP_CALLBACK_INSTANCE, void* context) {
    const std::unique_ptr<Request> request(static_cast<Request*>(context));
    auto* lyrics = new Lyrics(Find(*request));
    if (!PostMessageW(request->notify, LyricsSource::kMessage, request->generation, reinterpret_cast<LPARAM>(lyrics))) delete lyrics;
}

}  // namespace

void LyricsSource::LookUp(HWND notify, uintptr_t generation, const std::string& title, const std::string& artist,
                          const std::string& album, double duration) {
    auto* request = new Request{notify, generation, CleanTitle(title), artist, album, duration};
    if (!TrySubmitThreadpoolCallback(Run, request, nullptr)) delete request;
}

}  // namespace noitch
