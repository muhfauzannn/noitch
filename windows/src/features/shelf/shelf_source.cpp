#include "shelf_source.h"
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <cstring>

namespace noitch {
namespace {
std::wstring Wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    if (n) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string Utf8(const std::wstring& s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::vector<uint8_t> Read(const std::wstring& path, DWORD limit = 4000000) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    const DWORD length = GetFileSize(file, nullptr);
    std::vector<uint8_t> bytes;
    if (length <= limit) {
        bytes.resize(length);
        DWORD read = 0;
        if (!ReadFile(file, bytes.data(), length, &read, nullptr) || read != length) bytes.clear();
    }
    CloseHandle(file);
    return bytes;
}
bool Write(const std::wstring& path, const std::vector<uint8_t>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0;
    const bool success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size();
    CloseHandle(file);
    return success;
}
std::string Fingerprint(const std::vector<uint8_t>& bytes, ShelfKind kind) {
    // A second independent accumulator reduces collisions without loading a hashing provider at rest.
    uint64_t a = 14695981039346656037ull, b = 1099511628211ull;
    for (const uint8_t byte : bytes) { a = (a ^ byte) * 1099511628211ull; b = (b + byte) * 14029467366897019727ull; }
    char id[48];
    std::snprintf(id, sizeof(id), "%u-%016llx%016llx", static_cast<unsigned>(kind), static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    return id;
}
void Number(std::vector<uint8_t>& bytes, uint32_t n) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(n >> (i * 8))); }
void String(std::vector<uint8_t>& bytes, const std::string& value) { Number(bytes, static_cast<uint32_t>(value.size())); bytes.insert(bytes.end(), value.begin(), value.end()); }
class Reader {
public:
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool good = true;
    uint32_t Number() {
        if (at + 4 > bytes.size()) { good = false; return 0; }
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(bytes[at++]) << (i * 8);
        return n;
    }
    std::string String() {
        const size_t n = Number();
        if (n > 64000 || at + n > bytes.size()) { good = false; return {}; }
        std::string text(bytes.begin() + at, bytes.begin() + at + n); at += n; return text;
    }
};
bool Put(UINT type, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return false;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return false;
    void* target = GlobalLock(memory);
    if (!target) { GlobalFree(memory); return false; }
    std::memcpy(target, bytes.data(), bytes.size()); GlobalUnlock(memory);
    if (!SetClipboardData(type, memory)) { GlobalFree(memory); return false; }
    return true;
}
}

void ShelfSource::Initialize(HWND window) {
    window_ = window;
    ownMarker_ = RegisterClipboardFormatW(L"app.noitch.clipboard");
    wchar_t* base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        directory_ = std::wstring(base) + L"\\Noitch"; CoTaskMemFree(base);
        CreateDirectoryW(directory_.c_str(), nullptr);
        directory_ += L"\\Shelf"; CreateDirectoryW(directory_.c_str(), nullptr);
    }
    DWORD paused = 0, size = sizeof(paused);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Noitch", L"shelfPaused", RRF_RT_REG_DWORD, nullptr, &paused, &size);
    SetRecording(paused == 0);
}
void ShelfSource::SetRecording(bool recording) {
    if (recording_) RemoveClipboardFormatListener(window_);
    recording_ = recording && AddClipboardFormatListener(window_);
    sequence_ = GetClipboardSequenceNumber();
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Noitch", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
        const DWORD paused = recording_ ? 0 : 1;
        RegSetValueExW(key, L"shelfPaused", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&paused), sizeof(paused)); RegCloseKey(key);
    }
}
void ShelfSource::Stop() { if (recording_) RemoveClipboardFormatListener(window_); recording_ = false; }

ShelfItem ShelfSource::Files(const std::vector<std::string>& paths) const {
    ShelfItem item;
    item.kind = ShelfKind::Files;
    item.files.assign(paths.begin(), paths.begin() + std::min(paths.size(), size_t{16}));
    if (item.files.empty()) return item;
    const auto& first = item.files.front();
    const auto slash = first.find_last_of("/\\");
    item.title = item.files.size() > 1 ? std::to_string(item.files.size()) + " files" : first.substr(slash == std::string::npos ? 0 : slash + 1);
    item.preview = item.title;
    item.id = Fingerprint(FilePayload(item.files), item.kind);
    return item;
}
std::vector<std::string> ShelfSource::PathsFromDrop(HDROP drop) {
    std::vector<std::string> paths;
    for (UINT i = 0, count = std::min(DragQueryFileW(drop, 0xffffffff, nullptr, 0), 16u); i < count; ++i) {
        const UINT n = DragQueryFileW(drop, i, nullptr, 0);
        if (!n || n > 4096) continue;
        std::wstring path(n + 1, L'\0'); DragQueryFileW(drop, i, path.data(), n + 1); path.resize(n);
        paths.push_back(Utf8(path));
    }
    return paths;
}
std::vector<uint8_t> ShelfSource::FilePayload(const std::vector<std::string>& paths) {
    std::wstring all;
    for (const auto& path : paths) { all += Wide(path); all.push_back(L'\0'); }
    all.push_back(L'\0');
    DROPFILES header{}; header.pFiles = sizeof(header); header.fWide = TRUE;
    std::vector<uint8_t> bytes(sizeof(header) + all.size() * sizeof(wchar_t));
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), all.data(), all.size() * sizeof(wchar_t));
    return bytes;
}
bool ShelfSource::Store(const std::vector<uint8_t>& bytes, ShelfItem& item) {
    item.id = Fingerprint(bytes, item.kind);
    item.payload = "payload-" + item.id;
    if (directory_.empty()) return false;
    const auto path = directory_ + L"\\" + Wide(item.payload);
    const auto temp = path + L".tmp";
    return Write(temp, bytes) && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
std::vector<uint8_t> ShelfSource::Payload(const ShelfItem& item) const {
    if (item.kind == ShelfKind::Files) return FilePayload(item.files);
    if (directory_.empty()) return {};
    if (item.payload.rfind("payload-", 0) != 0 || item.payload.find_first_of("/\\") != std::string::npos) return {};
    return Read(directory_ + L"\\" + Wide(item.payload));
}
bool ShelfSource::Capture(ShelfItem* item, bool force) {
    const DWORD sequence = GetClipboardSequenceNumber();
    if ((!force && (!recording_ || sequence == sequence_)) || !OpenClipboard(window_)) return false;
    sequence_ = sequence;
    bool allowed = !IsClipboardFormatAvailable(ownMarker_);
    for (const wchar_t* name : {L"ExcludeClipboardContentFromMonitorProcessing", L"org.nspasteboard.ConcealedType", L"org.nspasteboard.TransientType"}) allowed = allowed && !IsClipboardFormatAvailable(RegisterClipboardFormatW(name));
    bool captured = false;
    if (allowed && IsClipboardFormatAvailable(CF_HDROP)) {
        *item = Files(PathsFromDrop(static_cast<HDROP>(GetClipboardData(CF_HDROP))));
        captured = !item->files.empty();
    } else if (allowed) {
        UINT format = IsClipboardFormatAvailable(CF_UNICODETEXT) ? CF_UNICODETEXT : CF_DIB;
        HANDLE data = GetClipboardData(format);
        const SIZE_T size = data ? GlobalSize(data) : 0;
        if (size > 0 && size <= (format == CF_UNICODETEXT ? 128000u : 4000000u)) {
            const void* raw = GlobalLock(data);
            if (raw) {
                std::vector<uint8_t> bytes(static_cast<const uint8_t*>(raw), static_cast<const uint8_t*>(raw) + size);
                GlobalUnlock(data);
                if (format == CF_UNICODETEXT) {
                    std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t));
                    const auto end = text.find(L'\0');
                    if (end != std::wstring::npos) text.resize(end);
                    bytes.resize((text.size() + 1) * sizeof(wchar_t));
                    std::memcpy(bytes.data(), text.c_str(), bytes.size());
                    const std::string string = Utf8(text);
                    item->kind = ((string.rfind("https://", 0) == 0 && string.size() > 8) || (string.rfind("http://", 0) == 0 && string.size() > 7)) && string.find_first_of(" \t\r\n") == std::string::npos ? ShelfKind::Link : ShelfKind::Text;
                    item->title = item->kind == ShelfKind::Link ? "Link" : "Text";
                    item->preview = Utf8(text.substr(0, 150));
                    captured = string.find_first_not_of(" \t\r\n") != std::string::npos && string.size() <= 64000 && Store(bytes, *item);
                } else {
                    item->kind = ShelfKind::Image; item->title = "Copied image"; item->preview = "Bitmap image";
                    captured = size >= sizeof(BITMAPINFOHEADER) && Store(bytes, *item);
                }
            }
        }
    }
    CloseClipboard();
    return captured;
}
bool ShelfSource::Copy(const ShelfItem& item) {
    if (item.kind == ShelfKind::Files) {
        for (const auto& path : item.files) if (GetFileAttributesW(Wide(path).c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    }
    const auto bytes = Payload(item);
    if (bytes.empty() || !OpenClipboard(window_)) return false;
    EmptyClipboard();
    const UINT format = item.kind == ShelfKind::Files ? CF_HDROP : item.kind == ShelfKind::Image ? CF_DIB : CF_UNICODETEXT;
    const bool result = Put(format, bytes);
    Put(ownMarker_, {1});
    sequence_ = GetClipboardSequenceNumber(); CloseClipboard();
    return result;
}
void ShelfSource::Load(std::vector<ShelfItem>& files, std::vector<ShelfItem>& clipboard) {
    if (directory_.empty()) return;
    const auto data = Read(directory_ + L"\\items.bin", 2000000);
    if (data.empty()) return;
    Reader reader{data};
    if (reader.Number() != 0x3153484e) return;
    std::vector<ShelfItem> loaded[2];
    for (auto& collection : loaded) {
        const uint32_t count = reader.Number();
        if (count > kShelfCapacity) return;
        for (uint32_t i = 0; i < count && reader.good; ++i) {
            ShelfItem item;
            const uint32_t kind = reader.Number(); if (kind > 3) return;
            item.kind = static_cast<ShelfKind>(kind); item.pinned = reader.Number() != 0;
            item.id = reader.String(); item.title = reader.String(); item.preview = reader.String(); item.payload = reader.String();
            const uint32_t n = reader.Number(); if (n > 16) return;
            for (uint32_t j = 0; j < n; ++j) item.files.push_back(reader.String());
            collection.push_back(std::move(item));
        }
    }
    if (reader.good) { files = std::move(loaded[0]); clipboard = std::move(loaded[1]); }
}
void ShelfSource::Save(const std::vector<ShelfItem>& files, const std::vector<ShelfItem>& clipboard) {
    if (directory_.empty()) return;
    std::vector<uint8_t> bytes;
    Number(bytes, 0x3153484e);
    for (const auto* collection : {&files, &clipboard}) {
        Number(bytes, static_cast<uint32_t>(collection->size()));
        for (const auto& item : *collection) {
            Number(bytes, static_cast<uint32_t>(item.kind)); Number(bytes, item.pinned ? 1 : 0);
            String(bytes, item.id); String(bytes, item.title); String(bytes, item.preview); String(bytes, item.payload);
            Number(bytes, static_cast<uint32_t>(item.files.size())); for (const auto& path : item.files) String(bytes, path);
        }
    }
    const auto temp = directory_ + L"\\items.tmp";
    if (Write(temp, bytes)) MoveFileExW(temp.c_str(), (directory_ + L"\\items.bin").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    WIN32_FIND_DATAW found{};
    HANDLE search = FindFirstFileW((directory_ + L"\\payload-*").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            const std::string name = Utf8(found.cFileName);
            bool keep = false;
            for (const auto* collection : {&files, &clipboard}) for (const auto& item : *collection) keep = keep || item.payload == name;
            const std::wstring fullPath = directory_ + L"\\" + found.cFileName;
            for (const auto* collection : {&files, &clipboard}) for (const auto& item : *collection) for (const auto& path : item.files) {
                const auto wide = Wide(path);
                keep = keep || CompareStringOrdinal(fullPath.c_str(), -1, wide.c_str(), -1, TRUE) == CSTR_EQUAL;
            }
            if (!keep && !(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((directory_ + L"\\" + found.cFileName).c_str());
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
}
}
