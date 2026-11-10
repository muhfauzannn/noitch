#include "usage_scene.h"

#include "island/island_page.h"

namespace noitch {

namespace {

D2D1_COLOR_F White(float opacity) { return D2D1::ColorF(1, 1, 1, opacity); }

}  // namespace

HRESULT UsageScene::Create(Compositor& compositor, Painter& painter, const UsageLayout& layout, float left, float scale) {
    compositor_ = &compositor;
    painter_ = &painter;
    layout_ = layout;
    hasShown_ = false;
    constexpr auto leading = DWRITE_TEXT_ALIGNMENT_LEADING;
    constexpr auto trailing = DWRITE_TEXT_ALIGNMENT_TRAILING;
    HRESULT hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 12 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &titleFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 22 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, leading, &tokensFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 15 * scale, DWRITE_FONT_WEIGHT_MEDIUM, trailing, &costFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, leading, &smallFormat_))) return hr;
    if (FAILED(hr = painter.MakeFormat(L"Segoe UI", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, trailing, &smallRightFormat_))) return hr;
    return compositor.CreateLayerSurface(PageLayer(IslandPage::Usage), static_cast<UINT>(layout.Width() + 0.5f), static_cast<UINT>(layout.Height() + 0.5f), left, &surface_);
}

void UsageScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(PageLayer(IslandPage::Usage));
    surface_.Reset();
    titleFormat_.Reset();
    tokensFormat_.Reset();
    costFormat_.Reset();
    smallFormat_.Reset();
    smallRightFormat_.Reset();
}

void UsageScene::PaintColumn(int index, const UsageColumn& column) {
    painter_->Text(column.title, titleFormat_.Get(), layout_.Title(index), White(0.6f));
    painter_->Text(column.tokens, tokensFormat_.Get(), layout_.Tokens(index), White(1));
    painter_->Text(column.cost, costFormat_.Get(), layout_.Cost(index), White(0.85f));
    painter_->Text(column.limit, smallFormat_.Get(), layout_.Limit(index), White(0.6f));
    if (column.limitFraction) {
        const HomeRect track = layout_.LimitTrack(index);
        painter_->Fill(track, track.height / 2, White(0.2f));
        HomeRect filled = track;
        filled.width = track.width * static_cast<float>(*column.limitFraction);
        // Amber once most of the window is used, so a glance tells whether to slow down.
        const D2D1_COLOR_F fill = *column.limitFraction >= 0.8 ? D2D1::ColorF(1.0f, 0.62f, 0.04f, 1.0f) : White(1);
        if (filled.width > 0) painter_->Fill(filled, track.height / 2, fill);
    }
    for (int row = 0; row < column.rowCount; ++row) {
        painter_->Text(column.rows[row].name, smallFormat_.Get(), layout_.Row(index, row), White(0.8f));
        painter_->Text(column.rows[row].value, smallRightFormat_.Get(), layout_.Row(index, row), White(0.5f));
    }
}

HRESULT UsageScene::Paint(const UsageModel& model) {
    if (!surface_ || (hasShown_ && model == shown_)) return S_OK;
    HRESULT hr = painter_->Begin(surface_.Get(), layout_.Width(), layout_.Height());
    if (FAILED(hr)) return hr;
    PaintColumn(0, model.claude);
    PaintColumn(1, model.codex);
    if (FAILED(hr = painter_->End(surface_.Get()))) return hr;
    shown_ = model;
    hasShown_ = true;
    return compositor_->Commit();
}

}  // namespace noitch
