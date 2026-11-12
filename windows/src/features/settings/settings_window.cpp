#include "settings_window.h"

#include <windowsx.h>

#include <memory>

#include "features/home/home_settings_pane.h"
#include "features/tasks/moodle_source.h"
#include "features/tasks/tasks_settings_pane.h"
#include "features/usage/usage_settings_pane.h"
#include "settings_panes.h"
#include "settings_store.h"

namespace noitch {

namespace {

constexpr wchar_t kClassName[] = L"NoitchSettings";
constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
// Logical pixels.
constexpr int kWidth = 660;
constexpr int kHeight = 480;
constexpr int kSidebarWidth = 170;
constexpr int kPadding = 20;
constexpr int kRowHeight = 24;
constexpr int kCaptionHeight = 18;
constexpr int kChoiceWidth = 170;
constexpr int kSidebarId = 50;
constexpr int kFirstRowId = 1000;
constexpr int kSignInId = 60;
constexpr int kUseKeyId = 61;
constexpr int kSignOutId = 62;
constexpr int kSitePickerId = 63;

std::wstring WideText(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(length > 0 ? length : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    wide.resize(wcslen(wide.c_str()));
    return wide;
}

// The control's text as UTF-8. The buffer is wiped, as it may hold a password.
std::string ControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring wide(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, wide.data(), length + 1);
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), length, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), length, text.data(), size, nullptr, nullptr);
    SecureZeroMemory(wide.data(), wide.size() * sizeof(wchar_t));
    return text;
}

std::string Trimmed(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' || text.back() == '\t')) text.pop_back();
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) ++start;
    return text.substr(start);
}

}  // namespace

void SettingsWindow::Show(HINSTANCE instance, const AppSettings& settings) {
    settings_ = settings;
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kClassName;
        registered = RegisterClassExW(&windowClass) != 0;
    }
    panes_ = {GeneralSettingsPane(), PagesSettingsPane(), HomeSettingsPane(), UsageSettingsPane(), TasksSettingsPane(), AboutSettingsPane()};
    if (!CreateWindowExW(0, kClassName, L"Noitch Settings", kStyle, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, nullptr, nullptr, instance, this)) return;
    dpi_ = GetDpiForWindow(hwnd_);
    RECT frame{0, 0, Scaled(kWidth), Scaled(kHeight)};
    AdjustWindowRectExForDpi(&frame, kStyle, FALSE, 0, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_);
    font_ = CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW heading = metrics.lfMessageFont;
    heading.lfWeight = FW_SEMIBOLD;
    headingFont_ = CreateFontIndirectW(&heading);
    LOGFONTW title = heading;
    title.lfWeight = FW_BOLD;
    title.lfHeight = -Scaled(20);
    titleFont_ = CreateFontIndirectW(&title);
    LOGFONTW small = metrics.lfMessageFont;
    small.lfHeight = small.lfHeight * 11 / 12;
    smallFont_ = CreateFontIndirectW(&small);

    BuildSidebar();
    ShowPage(page_);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

int SettingsWindow::Scaled(int value) const { return MulDiv(value, static_cast<int>(dpi_), USER_DEFAULT_SCREEN_DPI); }

void SettingsWindow::BuildSidebar() {
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    sidebar_ = CreateWindowExW(0, L"LISTBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, Scaled(12),
                               Scaled(12), Scaled(kSidebarWidth - 12), Scaled(kHeight - 24), hwnd_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarId)), instance, nullptr);
    SendMessageW(sidebar_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    SendMessageW(sidebar_, LB_SETITEMHEIGHT, 0, Scaled(28));
    for (const SettingsPane& pane : panes_) SendMessageW(sidebar_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pane.title.c_str()));
    SendMessageW(sidebar_, LB_SETCURSEL, page_, 0);
}

void SettingsWindow::ShowPage(int index) {
    page_ = index;
    for (const HWND control : pageControls_) DestroyWindow(control);
    pageControls_.clear();
    secondary_.clear();
    rows_.clear();
    rowControls_.clear();
    sitePicker_ = siteEdit_ = userEdit_ = passwordEdit_ = keyEdit_ = signInButton_ = useKeyButton_ = statusLabel_ = nullptr;
    ++signInGeneration_;

    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    const int left = kSidebarWidth + kPadding;
    const int width = kWidth - left - kPadding;
    int y = 16;
    const auto add = [&](const wchar_t* type, const std::wstring& text, DWORD style, int id, int x, int top, int w, int h, HFONT font) {
        const HWND control = CreateWindowExW(0, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, Scaled(x), Scaled(top), Scaled(w), Scaled(h),
                                             hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        pageControls_.push_back(control);
        return control;
    };
    const auto caption = [&](const std::wstring& text, int top, int lines) {
        secondary_.push_back(add(L"STATIC", text, SS_LEFT, 0, left + 4, top, width - 8, kCaptionHeight * lines, smallFont_));
    };

    const SettingsPane& pane = panes_[static_cast<size_t>(index)];
    add(L"STATIC", pane.title, 0, 0, left, y, width, 30, titleFont_);
    y += 40;
    for (const SettingsSection& section : pane.sections) {
        if (!section.title.empty()) {
            add(L"STATIC", section.title, 0, 0, left, y, width, 20, headingFont_);
            y += 22;
        }
        for (const SettingsRow& row : section.rows) {
            const int id = kFirstRowId + static_cast<int>(rows_.size());
            HWND control = nullptr;
            switch (row.kind) {
                case SettingsRow::Kind::Toggle:
                case SettingsRow::Kind::LaunchAtLogin: {
                    control = add(L"BUTTON", row.title, BS_AUTOCHECKBOX | WS_TABSTOP, id, left + 4, y, width - 8, kRowHeight, font_);
                    const bool on = row.kind == SettingsRow::Kind::LaunchAtLogin ? LaunchesAtLogin() : row.isOn(settings_);
                    SendMessageW(control, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
                    break;
                }
                case SettingsRow::Kind::Choice: {
                    add(L"STATIC", row.title, SS_CENTERIMAGE, 0, left + 4, y, width - kChoiceWidth - 16, kRowHeight, font_);
                    control = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, id, left + width - kChoiceWidth, y, kChoiceWidth, 200, font_);
                    for (const auto& option : row.options) SendMessageW(control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.first.c_str()));
                    SendMessageW(control, CB_SETCURSEL, ChoiceIndex(row, row.value(settings_)), 0);
                    break;
                }
                case SettingsRow::Kind::Text:
                    add(L"STATIC", row.title, SS_CENTERIMAGE, 0, left + 4, y, width - 8, kRowHeight, font_);
                    break;
                case SettingsRow::Kind::Account:
                    y = BuildAccount(left, width, y) - kRowHeight - 2;
                    break;
            }
            rows_.push_back(&row);
            rowControls_.push_back(control);
            y += kRowHeight + 2;
            if (!row.caption.empty()) {
                caption(row.caption, y - 2, 1);
                y += kCaptionHeight;
            }
            y += 4;
        }
        if (!section.footer.empty()) {
            caption(section.footer, y, 2);
            y += kCaptionHeight * 2;
        }
        y += 12;
    }
    if (!pane.note.empty()) caption(pane.note, y, 2);
    UpdateEnabled();
}

int SettingsWindow::BuildAccount(int left, int width, int y) {
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE));
    const auto add = [&](const wchar_t* type, const std::wstring& text, DWORD style, int id, int x, int top, int w, int h, HFONT font, DWORD exStyle = 0) {
        const HWND control = CreateWindowExW(exStyle, type, text.c_str(), WS_CHILD | WS_VISIBLE | style, Scaled(x), Scaled(top), Scaled(w), Scaled(h), hwnd_,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        pageControls_.push_back(control);
        return control;
    };
    constexpr int kFieldWidth = 220;
    const int fieldX = left + width - kFieldWidth;
    const auto field = [&](const wchar_t* title, const std::string& value, DWORD extra) {
        add(L"STATIC", title, SS_CENTERIMAGE, 0, left + 4, y, width - kFieldWidth - 16, kRowHeight, font_);
        const HWND edit = add(L"EDIT", WideText(value), ES_AUTOHSCROLL | WS_TABSTOP | extra, 0, fieldX, y, kFieldWidth, kRowHeight, font_, WS_EX_CLIENTEDGE);
        y += kRowHeight + 6;
        return edit;
    };
    if (settings_.tasks.IsSignedIn()) {
        const std::wstring who = WideText(settings_.tasks.accountName + " \xC2\xB7 " + TasksSiteHost(settings_.tasks.site));
        add(L"STATIC", L"Signed in as " + who, SS_CENTERIMAGE, 0, left + 4, y, width - 110, kRowHeight, font_);
        add(L"BUTTON", L"Sign out", BS_PUSHBUTTON | WS_TABSTOP, kSignOutId, left + width - 96, y, 96, kRowHeight, font_);
        y += kRowHeight + 4;
        secondary_.push_back(add(L"STATIC", L"Signing out deletes the key from Credential Manager. To revoke it on the site too, reset it under Preferences \x203A Security keys.",
                                 SS_LEFT, 0, left + 4, y, width - 8, kCaptionHeight * 2, smallFont_));
        return y + kCaptionHeight * 2 + 4;
    }
    add(L"STATIC", L"Site", SS_CENTERIMAGE, 0, left + 4, y, width - kFieldWidth - 16, kRowHeight, font_);
    sitePicker_ = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, kSitePickerId, fieldX, y, kFieldWidth, 200, font_);
    int known = kKnownTaskSiteCount;
    for (int i = 0; i < kKnownTaskSiteCount; ++i) {
        SendMessageW(sitePicker_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(WideText(kKnownTaskSites[i][0]).c_str()));
        if (settings_.tasks.site == kKnownTaskSites[i][1]) known = i;
    }
    SendMessageW(sitePicker_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Other\x2026"));
    SendMessageW(sitePicker_, CB_SETCURSEL, known, 0);
    y += kRowHeight + 6;
    siteEdit_ = field(L"Address (for Other)", settings_.tasks.site, 0);
    EnableWindow(siteEdit_, known == kKnownTaskSiteCount);
    userEdit_ = field(L"Username", "", 0);
    passwordEdit_ = field(L"Password", "", ES_PASSWORD);
    signInButton_ = add(L"BUTTON", L"Sign in", BS_DEFPUSHBUTTON | WS_TABSTOP, kSignInId, left + width - 96, y, 96, kRowHeight, font_);
    statusLabel_ = add(L"STATIC", L"", SS_CENTERIMAGE, 0, left + 4, y, width - 112, kRowHeight, smallFont_);
    y += kRowHeight + 4;
    secondary_.push_back(add(L"STATIC", L"Your password is sent once to the site to get a key, and isn't stored. The key is kept in Credential Manager.", SS_LEFT, 0,
                             left + 4, y, width - 8, kCaptionHeight * 2, smallFont_));
    y += kCaptionHeight * 2 + 8;
    keyEdit_ = field(L"Or a security key", "", 0);
    useKeyButton_ = add(L"BUTTON", L"Use key", BS_PUSHBUTTON | WS_TABSTOP, kUseKeyId, left + width - 96, y, 96, kRowHeight, font_);
    y += kRowHeight + 4;
    secondary_.push_back(add(L"STATIC", L"From the site's Preferences \x203A Security keys: the key for the Moodle mobile web service.", SS_LEFT, 0, left + 4, y,
                             width - 8, kCaptionHeight * 2, smallFont_));
    return y + kCaptionHeight * 2 + 4;
}

void SettingsWindow::SiteChosen() {
    const auto index = static_cast<int>(SendMessageW(sitePicker_, CB_GETCURSEL, 0, 0));
    const bool known = index >= 0 && index < kKnownTaskSiteCount;
    EnableWindow(siteEdit_, !known);
    if (known) {
        SetWindowTextW(siteEdit_, WideText(kKnownTaskSites[index][1]).c_str());
    } else {
        SetWindowTextW(siteEdit_, L"");
        SetFocus(siteEdit_);
    }
    Report(L"");
}

void SettingsWindow::Report(const std::wstring& text) {
    if (statusLabel_) SetWindowTextW(statusLabel_, text.c_str());
}

void SettingsWindow::StartSignIn(bool withKey) {
    if (!siteEdit_) return;
    const std::string site = NormalizedTasksSite(ControlText(siteEdit_));
    if (site.empty()) {
        Report(L"The site needs to be an https address, such as scele.cs.ui.ac.id.");
        return;
    }
    pendingSite_ = site;
    if (withKey) {
        std::string key = Trimmed(ControlText(keyEdit_));
        if (key.empty()) {
            Report(L"Paste the key first.");
            return;
        }
        Report(L"Checking the key\x2026");
        MoodleSource::UseKey(hwnd_, ++signInGeneration_, site, std::move(key));
    } else {
        const std::string user = Trimmed(ControlText(userEdit_));
        std::string password = ControlText(passwordEdit_);
        if (user.empty() || password.empty()) {
            SecureZeroMemory(password.data(), password.size());
            Report(L"Enter your username and password.");
            return;
        }
        // The password lives only in this request; the field forgets it now.
        SetWindowTextW(passwordEdit_, L"");
        Report(L"Signing in\x2026");
        MoodleSource::SignIn(hwnd_, ++signInGeneration_, site, user, std::move(password));
    }
    EnableWindow(signInButton_, FALSE);
    EnableWindow(useKeyButton_, FALSE);
}

void SettingsWindow::FinishSignIn(LPARAM lparam) {
    std::unique_ptr<MoodleSource::Result> result(reinterpret_cast<MoodleSource::Result*>(lparam));
    if (!result) return;
    if (signInButton_) EnableWindow(signInButton_, TRUE);
    if (useKeyButton_) EnableWindow(useKeyButton_, TRUE);
    std::wstring message;
    switch (result->problem) {
        case MoodleProblem::None: break;
        case MoodleProblem::WrongLogin: message = L"Wrong username or password."; break;
        case MoodleProblem::NoToken: message = L"The site didn't issue a key (" + WideText(result->message) + L"). Try a security key instead."; break;
        case MoodleProblem::SignedOut: message = L"The site refused this key."; break;
        case MoodleProblem::Maintenance: message = L"The site is under maintenance. Try again later."; break;
        default: message = L"Couldn't reach the site."; break;
    }
    if (message.empty() && !result->info.functions.empty() && !result->info.functions.count("core_calendar_get_action_events_by_timesort")) {
        message = L"This site doesn't let this account read its task list.";
    }
    if (message.empty() && !task_token::Save(pendingSite_, result->token)) message = L"Couldn't keep the key in Credential Manager.";
    SecureZeroMemory(result->token.data(), result->token.size());
    if (!message.empty()) {
        Report(message);
        return;
    }
    settings_.tasks.site = pendingSite_;
    settings_.tasks.accountName = result->info.userName.empty() ? "Signed in" : result->info.userName;
    SaveSettings(settings_);
    if (onChange) onChange(context, settings_);
    ShowPage(page_);
}

void SettingsWindow::SignOut() {
    task_token::Delete(settings_.tasks.site);
    settings_.tasks.accountName.clear();
    SaveSettings(settings_);
    if (onChange) onChange(context, settings_);
    ShowPage(page_);
}

void SettingsWindow::UpdateEnabled() {
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i]->enabled && rowControls_[i]) EnableWindow(rowControls_[i], rows_[i]->enabled(settings_));
    }
}

void SettingsWindow::Changed(int index, HWND control) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const SettingsRow& row = *rows_[static_cast<size_t>(index)];
    switch (row.kind) {
        case SettingsRow::Kind::LaunchAtLogin: {
            const bool on = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!SetLaunchesAtLogin(on)) SendMessageW(control, BM_SETCHECK, on ? BST_UNCHECKED : BST_CHECKED, 0);
            return;
        }
        case SettingsRow::Kind::Toggle:
            row.setOn(settings_, SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case SettingsRow::Kind::Choice: {
            const LRESULT selected = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (selected < 0 || selected >= static_cast<LRESULT>(row.options.size())) return;
            row.setValue(settings_, row.options[static_cast<size_t>(selected)].second);
            break;
        }
        case SettingsRow::Kind::Text:
        case SettingsRow::Kind::Account:
            return;
    }
    SaveSettings(settings_);
    UpdateEnabled();
    if (onChange) onChange(context, settings_);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT SettingsWindow::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (id == kSidebarId) {
                if (code == LBN_SELCHANGE) {
                    const LRESULT selected = SendMessageW(sidebar_, LB_GETCURSEL, 0, 0);
                    if (selected >= 0 && selected != page_) ShowPage(static_cast<int>(selected));
                }
                return 0;
            }
            if (code == BN_CLICKED && (id == kSignInId || id == kUseKeyId)) {
                StartSignIn(id == kUseKeyId);
                return 0;
            }
            if (code == CBN_SELCHANGE && id == kSitePickerId) {
                SiteChosen();
                return 0;
            }
            if (code == BN_CLICKED && id == kSignOutId) {
                SignOut();
                return 0;
            }
            if (id >= kFirstRowId && (code == BN_CLICKED || code == CBN_SELCHANGE)) Changed(id - kFirstRowId, reinterpret_cast<HWND>(lparam));
            return 0;
        }
        case MoodleSource::kMessage:
            if (wparam == signInGeneration_) {
                FinishSignIn(lparam);
            } else {
                // A stale answer: its key was never saved, so it is wiped and dropped.
                std::unique_ptr<MoodleSource::Result> stale(reinterpret_cast<MoodleSource::Result*>(lparam));
                if (stale) SecureZeroMemory(stale->token.data(), stale->token.size());
            }
            return 0;
        case WM_CTLCOLORSTATIC: {
            // Captions and footers are secondary text.
            const HWND control = reinterpret_cast<HWND>(lparam);
            const HDC dc = reinterpret_cast<HDC>(wparam);
            for (const HWND secondary : secondary_) {
                if (secondary == control) SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
            }
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_DESTROY:
            pageControls_.clear();
            secondary_.clear();
            rows_.clear();
            rowControls_.clear();
            panes_.clear();
            for (HFONT* font : {&font_, &headingFont_, &titleFont_, &smallFont_}) {
                if (*font) DeleteObject(*font);
                *font = nullptr;
            }
            hwnd_ = nullptr;
            sidebar_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}  // namespace noitch
