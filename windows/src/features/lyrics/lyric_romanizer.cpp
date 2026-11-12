#include "lyric_romanizer.h"

#include <windows.h>
#include <msime.h>
#include <icu.h>
#include <wrl/client.h>
#include <unordered_map>

namespace noitch {
namespace {
std::wstring Widen(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
std::string Narrow(const std::wstring& text) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count, nullptr, nullptr);
    return result;
}
bool NeedsReading(const std::wstring& text) {
    for (wchar_t c : text) if (c > 0x024f && IsCharAlphaW(c)) return true;
    return false;
}
bool HasKana(const std::wstring& text) {
    for (wchar_t c : text) if (c >= 0x3040 && c <= 0x30ff) return true;
    return false;
}
bool HasHan(const std::wstring& text) {
    for (wchar_t c : text) if (c >= 0x3400 && c <= 0x9fff) return true;
    return false;
}
}

void RomanizeLyrics(Lyrics& lyrics) {
    bool needed = false;
    bool japanese = false;
    for (const auto& line : lyrics.synced) {
        const auto text = Widen(line.text);
        japanese = japanese || HasKana(text);
        needed = needed || NeedsReading(text);
    }
    if (!needed) return;
    // Explicit loading keeps ICU out of the startup working set and supports older Windows builds.
    const HMODULE library = LoadLibraryExW(L"icuin.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return;
    const auto open = reinterpret_cast<decltype(&utrans_openU)>(GetProcAddress(library, "utrans_openU"));
    const auto transform = reinterpret_cast<decltype(&utrans_transUChars)>(GetProcAddress(library, "utrans_transUChars"));
    const auto close = reinterpret_cast<decltype(&utrans_close)>(GetProcAddress(library, "utrans_close"));
    if (!open || !transform || !close) { FreeLibrary(library); return; }
    UErrorCode status = U_ZERO_ERROR;
    const char16_t id[] = u"Any-Latin; NFD; [:Nonspacing Mark:] Remove; NFC";
    UTransliterator* latin = open(reinterpret_cast<const UChar*>(id), -1, UTRANS_FORWARD, nullptr, 0, nullptr, &status);
    if (!latin || U_FAILURE(status)) { FreeLibrary(library); return; }
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Microsoft::WRL::ComPtr<IFELanguage> ime;
    bool imeOpen = false;
    if (japanese && SUCCEEDED(apartment)) {
        CLSID clsid{};
        constexpr IID kLanguageId{0x019f7152, 0xe6db, 0x11d0, {0x83, 0xc3, 0x00, 0xc0, 0x4f, 0xdd, 0xb8, 0x2e}};
        if (SUCCEEDED(CLSIDFromProgID(L"MSIME.Japan", &clsid)) &&
            SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, kLanguageId, reinterpret_cast<void**>(ime.GetAddressOf())))) {
            imeOpen = SUCCEEDED(ime->Open());
        }
    }
    std::unordered_map<std::string, std::string> cache;
    for (auto& line : lyrics.synced) {
        if (const auto found = cache.find(line.text); found != cache.end()) { line.romanization = found->second; continue; }
        auto reading = Widen(line.text);
        bool usable = NeedsReading(reading);
        if (japanese && HasHan(reading)) {
            // Never display Mandarin readings as Japanese when the optional Japanese IME is absent.
            BSTR phonetic = nullptr;
            const BSTR input = SysAllocStringLen(reading.data(), static_cast<UINT>(reading.size()));
            usable = input && imeOpen && SUCCEEDED(ime->GetPhonetic(input, 1, -1, &phonetic)) && phonetic;
            SysFreeString(input);
            if (usable) reading.assign(phonetic, SysStringLen(phonetic));
            SysFreeString(phonetic);
            usable = usable && !HasHan(reading);
        }
        if (usable && !reading.empty()) {
            std::vector<UChar> buffer(reading.size() * 32 + 128);
            std::copy(reading.begin(), reading.end(), buffer.begin());
            int32_t length = static_cast<int32_t>(reading.size());
            int32_t limit = length;
            status = U_ZERO_ERROR;
            transform(latin, buffer.data(), &length, static_cast<int32_t>(buffer.size()), 0, &limit, &status);
            if (U_SUCCESS(status)) {
                const std::wstring result(buffer.begin(), buffer.begin() + length);
                const auto converted = Narrow(result);
                if (converted != line.text) line.romanization = converted;
            }
        }
        cache.emplace(line.text, line.romanization);
    }
    if (imeOpen) ime->Close();
    ime.Reset();
    if (SUCCEEDED(apartment)) CoUninitialize();
    close(latin);
    FreeLibrary(library);
}
}
