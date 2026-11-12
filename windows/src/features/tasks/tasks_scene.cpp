#include "tasks_scene.h"

#include <algorithm>
#include <string>

#include "island/island_page.h"

namespace noitch {

namespace {

D2D1_COLOR_F White(float opacity) { return D2D1::ColorF(1, 1, 1, opacity); }

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F color, float alpha) { return {color.r, color.g, color.b, alpha}; }

}  // namespace

HRESULT TasksScene::Create(Compositor& compositor, Painter& painter, const TasksLayout& layout, float left, float scale) {
    compositor_ = &compositor;
    painter_ = &painter;
    layout_ = layout;
    hasShown_ = false;
    constexpr auto leading = DWRITE_TEXT_ALIGNMENT_LEADING;
    constexpr auto center = DWRITE_TEXT_ALIGNMENT_CENTER;
    constexpr auto trailing = DWRITE_TEXT_ALIGNMENT_TRAILING;
    HRESULT hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 9.5f * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &labelFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 34 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &countdownFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 13 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &titleFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 9.5f * scale, DWRITE_FONT_WEIGHT_BOLD, center, &chipFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, leading, &smallFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 10.5f * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, center, &buttonFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 8.5f * scale, DWRITE_FONT_WEIGHT_MEDIUM, center, &dayFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11.5f * scale, DWRITE_FONT_WEIGHT_MEDIUM, leading, &rowFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &captionFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 10.5f * scale, DWRITE_FONT_WEIGHT_NORMAL, trailing, &dueFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 10 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, trailing, &toggleFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe MDL2 Assets", 10 * scale, DWRITE_FONT_WEIGHT_NORMAL, center, &iconFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 17 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, center, &messageFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11.5f * scale, DWRITE_FONT_WEIGHT_NORMAL, center, &detailFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe MDL2 Assets", 16 * scale, DWRITE_FONT_WEIGHT_NORMAL, center, &checkFormat_))) return hr;
    return compositor.CreateLayerSurface(PageLayer(IslandPage::Tasks), static_cast<UINT>(layout.Width() + 0.5f), static_cast<UINT>(layout.Height() + 0.5f), left, &surface_);
}

void TasksScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(PageLayer(IslandPage::Tasks));
    surface_.Reset();
    labelFormat_.Reset();
    countdownFormat_.Reset();
    titleFormat_.Reset();
    chipFormat_.Reset();
    smallFormat_.Reset();
    buttonFormat_.Reset();
    dayFormat_.Reset();
    rowFormat_.Reset();
    captionFormat_.Reset();
    dueFormat_.Reset();
    toggleFormat_.Reset();
    iconFormat_.Reset();
    messageFormat_.Reset();
    detailFormat_.Reset();
    checkFormat_.Reset();
    hasShown_ = false;
}

HRESULT TasksScene::Paint(const TasksPageModel& model, int hoveredRow) {
    if (!surface_ || (hasShown_ && model == shown_ && hoveredRow == shownHover_)) return S_OK;
    shown_ = model;
    shownHover_ = hoveredRow;
    hasShown_ = true;
    HRESULT hr = painter_->Begin(surface_.Get(), layout_.Width(), layout_.Height());
    if (FAILED(hr)) return hr;
    const float s = layout_.Scale();

    if (model.isMessage) {
        if (model.allClear) {
            const HomeRect check = layout_.Check();
            painter_->Arc({check.x + check.width / 2, check.y + check.height / 2}, check.width / 2 - 1.5f * s, 2 * s, 1, task_colors::kAccent);
            painter_->Glyph(L"\xE73E", checkFormat_.Get(), {check.x, check.y + check.height / 2 - 11 * s, check.width, 22 * s}, task_colors::kAccent);
        }
        painter_->Text(model.title, messageFormat_.Get(), layout_.Message(model.allClear), White(1));
        painter_->Text(model.detail, detailFormat_.Get(), layout_.MessageDetail(model.allClear), White(0.55f));
        if (!model.button.empty()) {
            const HomeRect button = layout_.MessageButton(model.allClear);
            painter_->Fill(button, button.height / 2, White(1));
            painter_->Text(model.button, buttonFormat_.Get(), {button.x, button.y + 4 * s, button.width, button.height - 8 * s}, D2D1::ColorF(0, 0, 0, 1));
        }
    } else {
        // The hero.
        std::string countdown;
        for (const auto& [value, unit] : model.countdown) {
            if (!countdown.empty()) countdown += ' ';
            countdown += std::to_string(value) + unit;
        }
        std::string label = model.heroLabel;
        for (char& c : label) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        painter_->Text(label, labelFormat_.Get(), layout_.HeroLabel(), White(0.45f));
        painter_->Text(countdown, countdownFormat_.Get(), layout_.Countdown(), task_colors::For(model.heroSeverity));
        painter_->Text(model.heroTitle, titleFormat_.Get(), layout_.HeroTitle(), White(1));
        const HomeRect chip = layout_.HeroCourse();
        painter_->Fill(chip, 4 * s, WithAlpha(task_colors::kAccent, 0.22f));
        painter_->Text(model.heroCourse, chipFormat_.Get(), {chip.x, chip.y + 0.5f * s, chip.width, chip.height}, White(1));
        painter_->Text(model.heroDue, smallFormat_.Get(), layout_.HeroDue(), White(0.55f));
        const HomeRect open = layout_.OpenButton();
        painter_->Fill(open, open.height / 2, White(1));
        painter_->Text("Open in " + model.siteName + " \xE2\x86\x97", buttonFormat_.Get(), {open.x, open.y + 3.5f * s, open.width, open.height - 7 * s}, D2D1::ColorF(0, 0, 0, 1));

        // The week ahead: the busiest day in the accent, a day with something due within a day in red,
        // the rest grey. The hovered row's day is drawn wider.
        int most = 1;
        for (const auto& day : model.load) most = std::max(most, day.count);
        int hoveredDay = -1;
        if (hoveredRow >= 0 && hoveredRow < static_cast<int>(model.rows.size())) hoveredDay = model.rows[static_cast<size_t>(hoveredRow)].day;
        for (size_t i = 0; i < model.load.size(); ++i) {
            const auto& day = model.load[i];
            const int index = static_cast<int>(i);
            HomeRect bar = layout_.Bar(index, static_cast<float>(day.count) / static_cast<float>(most));
            if (index == hoveredDay) {
                bar.x -= bar.width * 0.125f;
                bar.width *= 1.25f;
            }
            const D2D1_COLOR_F color = day.count == 0 ? White(0.12f) : day.hasUrgent ? task_colors::kCritical : day.isBusiest ? task_colors::kAccent : White(0.45f);
            if (day.hasNew) painter_->Fill({bar.x - 1.5f * s, bar.y - 1.5f * s, bar.width + 3 * s, bar.height + 3 * s}, 4 * s, White(1));
            painter_->Fill(bar, 3 * s, color);
            painter_->Text(day.letter, dayFormat_.Get(), layout_.BarLetter(index), White(index == 0 ? 0.9f : 0.4f));
        }
        painter_->Text(model.loadCaption, captionFormat_.Get(), layout_.LoadCaption(), White(1));
        painter_->Text(model.loadDetail, smallFormat_.Get(), layout_.LoadDetail(), White(0.5f));

        // The list.
        for (size_t i = 0; i < model.rows.size(); ++i) {
            const auto& row = model.rows[i];
            const int index = static_cast<int>(i);
            if (index == hoveredRow) painter_->Fill(layout_.Row(index), 7 * s, White(0.08f));
            const HomeRect icon = layout_.RowIcon(index);
            painter_->Fill(icon, 5 * s, WithAlpha(task_colors::kAccent, 0.18f));
            painter_->Glyph(task_colors::Glyph(row.module), iconFormat_.Get(), {icon.x, icon.y + 2 * s, icon.width, icon.height - 2 * s}, task_colors::kAccent);
            HomeRect title = layout_.RowTitle(index);
            if (row.isNew) {
                painter_->Dot({title.x + 2.5f * s, title.y + title.height / 2}, 2.5f * s, task_colors::kFresh);
                title.x += 9 * s;
                title.width -= 9 * s;
            }
            painter_->Text(row.title + "  \xC2\xB7 " + row.course, rowFormat_.Get(), title, White(1));
            painter_->Text(row.due, dueFormat_.Get(), layout_.RowDue(index), row.severity == Severity::Critical ? task_colors::kCritical : White(0.6f));
        }
        painter_->Text(model.more, smallFormat_.Get(), layout_.More(), White(0.4f));
        // Red while there is overdue work to reveal; quiet once it shows.
        const bool revealing = model.overdueToggle.rfind("Show", 0) == 0;
        painter_->Text(model.overdueToggle, toggleFormat_.Get(), layout_.OverdueToggle(), revealing ? task_colors::kCritical : White(0.5f));
    }
    if (FAILED(hr = painter_->End(surface_.Get()))) return hr;
    return compositor_->Commit();
}

}  // namespace noitch
