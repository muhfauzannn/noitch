#include "usage_scene.h"

#include <cmath>

#include "island/island_page.h"

namespace noitch {

namespace {

D2D1_COLOR_F White(float opacity) { return D2D1::ColorF(1, 1, 1, opacity); }

// Claude's coral and a calm blue for Codex, so the two read apart at a glance.
constexpr D2D1_COLOR_F kAccents[2] = {{0.85f, 0.47f, 0.34f, 1}, {0.48f, 0.64f, 1.0f, 1}};
constexpr D2D1_COLOR_F kWatch = {1.0f, 0.8f, 0.2f, 1};
constexpr D2D1_COLOR_F kCritical = {1.0f, 0.33f, 0.3f, 1};
constexpr D2D1_COLOR_F kUnder = {0.45f, 0.85f, 0.6f, 1};

D2D1_COLOR_F SeverityColor(Severity severity, D2D1_COLOR_F calm) {
    return severity == Severity::Critical ? kCritical : severity == Severity::Watch ? kWatch : calm;
}

// The week is the same hue, lighter, so the two rings read as one tool's two windows.
D2D1_COLOR_F Lighter(D2D1_COLOR_F color) {
    return {color.r + (1 - color.r) * 0.45f, color.g + (1 - color.g) * 0.45f, color.b + (1 - color.b) * 0.45f, 1};
}

}  // namespace

HRESULT UsageScene::Create(Compositor& compositor, Painter& painter, const UsageLayout& layout, float left, float scale) {
    compositor_ = &compositor;
    painter_ = &painter;
    layout_ = layout;
    hasShown_ = false;
    constexpr auto leading = DWRITE_TEXT_ALIGNMENT_LEADING;
    constexpr auto center = DWRITE_TEXT_ALIGNMENT_CENTER;
    HRESULT hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 20 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, center, &centerFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 10 * scale, DWRITE_FONT_WEIGHT_MEDIUM, center, &captionFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 13 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &nameFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, leading, &smallFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &smallBoldFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, &smallRightFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 9 * scale, DWRITE_FONT_WEIGHT_BOLD, center, &paceFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 12 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &headlineFormat_))) return hr;
    return compositor.CreateLayerSurface(PageLayer(IslandPage::Usage), static_cast<UINT>(layout.Width() + 0.5f), static_cast<UINT>(layout.Height() + 0.5f), left, &surface_);
}

void UsageScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(PageLayer(IslandPage::Usage));
    surface_.Reset();
    centerFormat_.Reset();
    captionFormat_.Reset();
    nameFormat_.Reset();
    smallFormat_.Reset();
    smallBoldFormat_.Reset();
    smallRightFormat_.Reset();
    paceFormat_.Reset();
    headlineFormat_.Reset();
}

void UsageScene::PaintTool(int index, const UsageModel::Tool& tool) {
    const float scale = layout_.Scale();
    const HomeRect gauge = layout_.Gauge(index);
    const D2D1_POINT_2F middle = D2D1::Point2F(gauge.x + gauge.width / 2, gauge.y + gauge.height / 2);
    const float width = UsageLayout::kRingWidth * scale;
    const D2D1_COLOR_F accent = kAccents[index];
    const auto ring = [&](const std::optional<UsageModel::Ring>& data, float radius, D2D1_COLOR_F calm) {
        painter_->Arc(middle, radius, width, 1, White(0.1f));
        if (!data) return;
        painter_->Arc(middle, radius, width, data->fraction, SeverityColor(data->severity, calm));
        if (data->tick) {
            const double angle = *data->tick * 2 * 3.14159265358979323846;
            const D2D1_POINT_2F at = D2D1::Point2F(middle.x + radius * static_cast<float>(std::sin(angle)), middle.y - radius * static_cast<float>(std::cos(angle)));
            painter_->Dot(at, 1.5f * scale, White(1));
        }
    };
    ring(tool.outer, UsageLayout::kOuterRadius * scale, Lighter(accent));
    ring(tool.inner, UsageLayout::kInnerRadius * scale, accent);

    painter_->Text(tool.center, centerFormat_.Get(), layout_.Center(index), White(1));
    painter_->Text(tool.caption, captionFormat_.Get(), layout_.Caption(index), White(0.5f));
    painter_->Text(tool.name, nameFormat_.Get(), layout_.Name(index), White(1));
    painter_->Text(tool.today, smallFormat_.Get(), layout_.Today(index), White(0.45f));
    for (int row = 0; row < 2; ++row) {
        const UsageModel::Row& data = tool.rows[row];
        painter_->Text(data.label, smallFormat_.Get(), layout_.RowLabel(index, row), White(0.55f));
        painter_->Text(data.value, smallBoldFormat_.Get(), layout_.RowValue(index, row), SeverityColor(data.severity, White(1)));
        if (data.pace) {
            // Ahead of the clock is the warning; behind it is room to spare.
            const char* symbol = *data.pace == Pace::Ahead ? "\xE2\x96\xB2" : *data.pace == Pace::Under ? "\xE2\x96\xBC" : "\xE2\x97\x8F";
            const D2D1_COLOR_F color = *data.pace == Pace::Ahead ? kWatch : *data.pace == Pace::Under ? kUnder : White(0.4f);
            painter_->Text(symbol, paceFormat_.Get(), layout_.RowPace(index, row), color);
        }
        painter_->Text(data.reset, smallRightFormat_.Get(), layout_.RowReset(index, row), White(0.55f));
    }
    painter_->Text(tool.headline, headlineFormat_.Get(), layout_.Headline(index), SeverityColor(tool.severity, White(1)));
    painter_->Text(tool.detail, smallFormat_.Get(), layout_.Detail(index), White(0.55f));
}

HRESULT UsageScene::Paint(const UsageModel& model) {
    if (!surface_ || (hasShown_ && model == shown_)) return S_OK;
    HRESULT hr = painter_->Begin(surface_.Get(), layout_.Width(), layout_.Height());
    if (FAILED(hr)) return hr;
    PaintTool(0, model.claude);
    PaintTool(1, model.codex);
    if (FAILED(hr = painter_->End(surface_.Get()))) return hr;
    shown_ = model;
    hasShown_ = true;
    return compositor_->Commit();
}

}  // namespace noitch
