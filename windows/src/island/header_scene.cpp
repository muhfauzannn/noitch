#include "header_scene.h"

namespace noitch {

namespace {

// Segoe MDL2 Assets ships with Windows 10 and 11, so icons need no image files.
constexpr wchar_t kGlyphMusic[] = L"\xEC4F";
constexpr wchar_t kGlyphUsage[] = L"\xE9D2";
constexpr wchar_t kGlyphSettings[] = L"\xE713";
constexpr float kIdleTabOpacity = 0.45f;

D2D1_COLOR_F White(float opacity) { return D2D1::ColorF(1, 1, 1, opacity); }

}  // namespace

HRESULT HeaderScene::Create(Compositor& compositor, Painter& painter, const HeaderLayout& layout, float width, float height, float left, float scale) {
    compositor_ = &compositor;
    painter_ = &painter;
    layout_ = layout;
    width_ = width;
    height_ = height;
    HRESULT hr = painter.MakeFormat(L"Segoe MDL2 Assets", 14 * scale, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, &iconFormat_);
    if (FAILED(hr)) return hr;
    return compositor.CreateLayerSurface(Compositor::kHeaderLayer, static_cast<UINT>(width + 0.5f), static_cast<UINT>(height + 0.5f), left, &surface_);
}

void HeaderScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(Compositor::kHeaderLayer);
    surface_.Reset();
    iconFormat_.Reset();
}

HRESULT HeaderScene::Paint(IslandPage page, HeaderControl hovered, bool isPressed) {
    if (!surface_) return S_OK;
    HRESULT hr = painter_->Begin(surface_.Get(), width_, height_);
    if (FAILED(hr)) return hr;
    const HomeRect selected = layout_.Frame(TabFor(page));
    painter_->Fill(selected, selected.height / 2, White(0.16f));
    if (hovered != HeaderControl::None) {
        const HomeRect frame = layout_.Frame(hovered);
        painter_->Fill(frame, frame.height / 2, White(isPressed ? 0.22f : 0.12f));
    }
    painter_->Glyph(kGlyphMusic, iconFormat_.Get(), layout_.Frame(HeaderControl::HomeTab), White(page == IslandPage::Home ? 1 : kIdleTabOpacity));
    if (layout_.ShowsUsage()) {
        painter_->Glyph(kGlyphUsage, iconFormat_.Get(), layout_.Frame(HeaderControl::UsageTab), White(page == IslandPage::Usage ? 1 : kIdleTabOpacity));
    }
    painter_->Glyph(kGlyphSettings, iconFormat_.Get(), layout_.Frame(HeaderControl::Settings), White(1));
    if (FAILED(hr = painter_->End(surface_.Get()))) return hr;
    return compositor_->Commit();
}

}  // namespace noitch
