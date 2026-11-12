#include "home_scene.h"

#include "island/island_page.h"

#include <d2d1_1helper.h>

#include <string>

using Microsoft::WRL::ComPtr;

namespace noitch {

namespace {

constexpr float kArtworkRadius = 14;
constexpr float kDimmedOpacity = 0.35f;
constexpr float kIdleOpacity = 0.55f;
constexpr float kSkippingOpacity = 0.4f;
constexpr float kNextLyricOpacity = 0.38f;
// The stage's lines by slot, from -1: the one before, the one being sung, the two after.
constexpr float kStageOpacity[] = {0.3f, 1.0f, 0.55f, 0.26f};
constexpr float kStageHoverOpacity = 0.85f;

// Segoe MDL2 Assets ships with Windows 10 and 11, so icons need no image files.
constexpr wchar_t kGlyphPlay[] = L"\xE768";
constexpr wchar_t kGlyphPause[] = L"\xE769";
constexpr wchar_t kGlyphPrevious[] = L"\xE892";
constexpr wchar_t kGlyphNext[] = L"\xE893";
constexpr wchar_t kGlyphShuffle[] = L"\xE8B1";
constexpr wchar_t kGlyphRepeatAll[] = L"\xE8EE";
constexpr wchar_t kGlyphRepeatOne[] = L"\xE8ED";
constexpr wchar_t kGlyphMusic[] = L"\xEC4F";
// Message: a speech bubble, standing for the words of the song.
constexpr wchar_t kGlyphLyrics[] = L"\xE8BD";

std::wstring Widen(const std::string& text) {
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}

D2D1_RECT_F ToD2D(const HomeRect& r) { return D2D1::RectF(r.x, r.y, r.Right(), r.Bottom()); }

D2D1_COLOR_F White(float opacity) { return D2D1::ColorF(1, 1, 1, opacity); }

D2D1_COLOR_F FromBgra(uint32_t color) {
    return D2D1::ColorF(static_cast<float>(color >> 16 & 0xFF) / 255, static_cast<float>(color >> 8 & 0xFF) / 255, static_cast<float>(color & 0xFF) / 255, 1);
}

}  // namespace

HRESULT HomeScene::Create(Compositor& compositor, const HomeLayout& layout, float left, float scale) {
    compositor_ = &compositor;
    layout_ = layout;
    scale_ = scale;

    HRESULT hr;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return hr;
    if (FAILED(hr = factory_->CreateDevice(compositor.DxgiDevice(), &device_))) return hr;
    if (FAILED(hr = device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_))) return hr;
    // Pixels are the unit everywhere here, so Direct2D must not scale again.
    context_->SetDpi(96, 96);
    if (FAILED(hr = context_->CreateSolidColorBrush(White(1), &brush_))) return hr;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())))) return hr;
    if (FAILED(hr = CreateFormats())) return hr;
    return compositor.CreateLayerSurface(PageLayer(IslandPage::Home), static_cast<UINT>(layout.Width() + 0.5f), static_cast<UINT>(layout.Height() + 0.5f), left, &surface_);
}

HRESULT HomeScene::CreateFormats() {
    const auto make = [this](const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment, IDWriteTextFormat** format) {
        HRESULT hr = writeFactory_->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * scale_, L"", format);
        if (FAILED(hr)) return hr;
        (*format)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        (*format)->SetTextAlignment(alignment);
        (*format)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(writeFactory_->CreateEllipsisTrimmingSign(*format, &ellipsis))) {
            const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            (*format)->SetTrimming(&trimming, ellipsis.Get());
        }
        return S_OK;
    };
    constexpr auto left = DWRITE_TEXT_ALIGNMENT_LEADING;
    constexpr auto center = DWRITE_TEXT_ALIGNMENT_CENTER;
    HRESULT hr;
    if (FAILED(hr = make(L"Segoe UI", 15, DWRITE_FONT_WEIGHT_SEMI_BOLD, left, &titleFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 13, DWRITE_FONT_WEIGHT_NORMAL, left, &artistFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, left, &lyricFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 12, DWRITE_FONT_WEIGHT_SEMI_BOLD, left, &nextLyricFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL, left, &timeFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, &remainingFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe MDL2 Assets", 15, DWRITE_FONT_WEIGHT_NORMAL, center, &iconFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe MDL2 Assets", 12, DWRITE_FONT_WEIGHT_NORMAL, center, &smallIconFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 20, DWRITE_FONT_WEIGHT_BOLD, center, &stageFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 14, DWRITE_FONT_WEIGHT_BOLD, center, &stageSideFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 16, DWRITE_FONT_WEIGHT_SEMI_BOLD, center, &stageStatusFormat_))) return hr;
    return make(L"Segoe MDL2 Assets", 21, DWRITE_FONT_WEIGHT_NORMAL, center, &largeIconFormat_);
}

void HomeScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveLayer(PageLayer(IslandPage::Home));
    surface_.Reset();
    artworkBrush_.Reset();
    titleFormat_.Reset();
    artistFormat_.Reset();
    lyricFormat_.Reset();
    timeFormat_.Reset();
    remainingFormat_.Reset();
    nextLyricFormat_.Reset();
    iconFormat_.Reset();
    largeIconFormat_.Reset();
    smallIconFormat_.Reset();
    stageFormat_.Reset();
    stageSideFormat_.Reset();
    stageStatusFormat_.Reset();
    writeFactory_.Reset();
    brush_.Reset();
    context_.Reset();
    device_.Reset();
    factory_.Reset();
}

void HomeScene::SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t barTint, uint32_t glow) {
    tint_ = barTint;
    glow_ = glow;
    artworkBrush_.Reset();
    if (!artwork || artwork->size <= 0 || !context_) return;
    const auto size = static_cast<UINT32>(artwork->size);
    const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(context_->CreateBitmap(D2D1::SizeU(size, size), artwork->pixels.data(), size * 4, &properties, &bitmap))) return;
    const HomeRect frame = layout_.Artwork();
    const float zoom = frame.width / static_cast<float>(size);
    const D2D1_BRUSH_PROPERTIES brush = D2D1::BrushProperties(1, D2D1::Matrix3x2F::Scale(zoom, zoom) * D2D1::Matrix3x2F::Translation(frame.x, frame.y));
    context_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(), brush, &artworkBrush_);
}

void HomeScene::PaintText(const std::string& text, IDWriteTextFormat* format, const HomeRect& rect, float opacity) {
    if (text.empty()) return;
    const std::wstring wide = Widen(text);
    brush_->SetColor(White(opacity));
    context_->DrawText(wide.c_str(), static_cast<UINT32>(wide.size()), format, ToD2D(rect), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void HomeScene::PaintIcon(const wchar_t* glyph, HomeControl control, float opacity, bool isLarge) {
    brush_->SetColor(White(opacity));
    context_->DrawText(glyph, 1, isLarge ? largeIconFormat_.Get() : iconFormat_.Get(), ToD2D(layout_.Frame(control)), brush_.Get());
}

// A glow of the artwork's colour, so the page takes on the track's mood. It fades out before the top
// edge, where the pill meets the black flares, so the bends show no seam. On the stage it is one soft
// wash instead, rising from the bottom edge and gone before the top.
void HomeScene::PaintGlow(const D2D1_RECT_F& whole, bool onStage) {
    if (glow_ == 0xFFFFFFFF || !artworkBrush_) return;
    if (onStage) {
        const D2D1_COLOR_F tint = FromBgra(glow_);
        const D2D1_GRADIENT_STOP stops[] = {
            {0.0f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.0f)},
            {0.4f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.1f)},
            {1.0f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.32f)},
        };
        ComPtr<ID2D1GradientStopCollection> collection;
        ComPtr<ID2D1LinearGradientBrush> wash;
        if (SUCCEEDED(context_->CreateGradientStopCollection(stops, 3, &collection)) &&
            SUCCEEDED(context_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, whole.top), D2D1::Point2F(0, whole.bottom)),
                                                          collection.Get(), &wash))) {
            context_->FillRectangle(whole, wash.Get());
        }
        return;
    }
    const D2D1_COLOR_F tint = FromBgra(glow_);
    const D2D1_GRADIENT_STOP stops[] = {
        {0.0f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.55f)},
        {0.45f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.2f)},
        {1.0f, D2D1::ColorF(tint.r, tint.g, tint.b, 0.0f)},
    };
    ComPtr<ID2D1GradientStopCollection> collection;
    ComPtr<ID2D1RadialGradientBrush> wash;
    const D2D1_POINT_2F center = D2D1::Point2F(layout_.GlowCenterX(), layout_.GlowCenterY());
    const float radiusX = layout_.GlowRadiusX();
    const float radiusY = layout_.GlowRadiusY();
    if (SUCCEEDED(context_->CreateGradientStopCollection(stops, 3, &collection)) &&
        SUCCEEDED(context_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(center, D2D1::Point2F(0, 0), radiusX, radiusY),
                                                      collection.Get(), &wash))) {
        context_->FillRectangle(whole, wash.Get());
    }
}

// The line being sung, large in the middle; the one before above it and the two after below, smaller
// and dimmer. Lines are redrawn in place; the slide macOS does would need a visual per line.
void HomeScene::PaintStage(const HomeModel& model, int stageHover) {
    for (int slot = kFirstStageSlot; slot <= kLastStageSlot; ++slot) {
        const std::string& text = model.stageLines[static_cast<size_t>(slot + 1)];
        if (slot == 0) {
            auto frame = layout_.StageLine(0);
            if (!model.stageRomanizations[1].empty()) frame.y -= 7 * scale_;
            PaintText(text, model.stageIsStatus ? stageStatusFormat_.Get() : stageFormat_.Get(), frame, model.stageIsStatus ? 0.55f : 1.0f);
            if (!model.stageRomanizations[1].empty()) {
                auto reading = layout_.StageLine(0);
                reading.y += 20 * scale_;
                reading.height = 18 * scale_;
                PaintText(model.stageRomanizations[1], stageSideFormat_.Get(), reading, 0.7f);
            }
            continue;
        }
        const float opacity = slot == stageHover ? kStageHoverOpacity : kStageOpacity[slot + 1];
        PaintText(text, stageSideFormat_.Get(), layout_.StageLine(slot), opacity);
    }
}

// A speech bubble at the end of the title row; on the stage it sits in the corner on a white disc,
// the way a switch that is on looks.
void HomeScene::PaintLyricsButton(const HomeModel& model, HomeControl hovered, bool isPressed) {
    if (!model.showsLyricsButton) return;
    const HomeRect frame = layout_.LyricsButton(model.isOnStage);
    const float radius = frame.width / 2;
    const D2D1_POINT_2F center = D2D1::Point2F(frame.x + radius, frame.y + radius);
    if (hovered == HomeControl::Lyrics) {
        brush_->SetColor(White(isPressed ? 0.22f : 0.12f));
        const float halo = radius + 3 * scale_;
        context_->FillEllipse(D2D1::Ellipse(center, halo, halo), brush_.Get());
    }
    if (model.isOnStage) {
        brush_->SetColor(White(1));
        context_->FillEllipse(D2D1::Ellipse(center, radius, radius), brush_.Get());
        brush_->SetColor(D2D1::ColorF(0, 0, 0, 1));
    } else {
        brush_->SetColor(White(0.75f));
    }
    context_->DrawText(kGlyphLyrics, 1, smallIconFormat_.Get(), ToD2D(frame), brush_.Get());
}

HRESULT HomeScene::Paint(const HomeModel& model, HomeControl hovered, bool isPressed, int stageHover) {
    if (!surface_) return S_OK;
    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    HRESULT hr = surface_->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
    if (FAILED(hr)) return hr;

    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target;
    if (FAILED(hr = context_->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &properties, &target))) {
        surface_->EndDraw();
        return hr;
    }
    context_->SetTarget(target.Get());
    context_->BeginDraw();
    // The surface can be a region of a shared atlas, so stay inside the area at `offset`.
    context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    const D2D1_RECT_F whole = D2D1::RectF(0, 0, layout_.Width(), layout_.Height());
    context_->PushAxisAlignedClip(whole, D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    PaintGlow(whole, model.isOnStage);
    PaintLyricsButton(model, hovered, isPressed);
    if (model.isOnStage) {
        PaintStage(model, stageHover);
        context_->PopAxisAlignedClip();
        hr = context_->EndDraw();
        context_->SetTarget(nullptr);
        const HRESULT end = surface_->EndDraw();
        if (FAILED(hr)) return hr;
        return FAILED(end) ? end : compositor_->Commit();
    }

    // Hover plate, under the icon.
    const bool plate = hovered == HomeControl::Previous || hovered == HomeControl::PlayPause || hovered == HomeControl::Next ||
                       ((hovered == HomeControl::Shuffle || hovered == HomeControl::Repeat) && model.showsShuffleAndRepeat);
    if (plate) {
        const HomeRect frame = layout_.Frame(hovered);
        brush_->SetColor(White(isPressed ? 0.22f : 0.12f));
        const float radius = frame.width / 2;
        context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(frame.x + radius, frame.y + radius), radius, radius), brush_.Get());
    }

    // While skipping, the old track stays, dimmed, until the player reports the new one.
    const float heading = model.isSkipping ? kSkippingOpacity : 1.0f;
    const HomeRect art = layout_.Artwork();
    const float artRadius = kArtworkRadius * scale_;
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToD2D(art), artRadius, artRadius);
    if (artworkBrush_) {
        artworkBrush_->SetOpacity(heading);
        context_->FillRoundedRectangle(rounded, artworkBrush_.Get());
    } else {
        brush_->SetColor(White(0.08f));
        context_->FillRoundedRectangle(rounded, brush_.Get());
        brush_->SetColor(White(kDimmedOpacity));
        context_->DrawText(kGlyphMusic, 1, largeIconFormat_.Get(), ToD2D(art), brush_.Get());
    }

    PaintText(model.title, titleFormat_.Get(), layout_.Title(), heading);
    PaintText(model.artist, artistFormat_.Get(), layout_.Artist(), 0.6f * heading);
    PaintText(model.lyric, lyricFormat_.Get(), layout_.Lyric(), model.lyricIsStatus ? 0.5f : 1.0f);
    PaintText(model.nextLyric, nextLyricFormat_.Get(), layout_.NextLyric(), kNextLyricOpacity);
    PaintText(model.elapsedText, timeFormat_.Get(), layout_.ElapsedTime(), 0.6f);
    PaintText(model.remainingText, remainingFormat_.Get(), layout_.RemainingTime(), 0.6f);

    const HomeRect track = layout_.SliderTrack();
    const float radius = track.height / 2;
    brush_->SetColor(White(0.18f));
    context_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(track), radius, radius), brush_.Get());
    HomeRect filled = track;
    filled.width = track.width * static_cast<float>(model.progress);
    brush_->SetColor(FromBgra(tint_));
    context_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(filled), radius, radius), brush_.Get());

    const float transport = model.hasTrack ? 1 : kDimmedOpacity;
    PaintIcon(kGlyphPrevious, HomeControl::Previous, transport, false);
    PaintIcon(model.isPlaying ? kGlyphPause : kGlyphPlay, HomeControl::PlayPause, transport, true);
    PaintIcon(kGlyphNext, HomeControl::Next, transport, false);
    if (model.showsShuffleAndRepeat) {
        PaintIcon(kGlyphShuffle, HomeControl::Shuffle, model.isShuffled ? 1 : kIdleOpacity, false);
        PaintIcon(model.repeatMode == RepeatMode::One ? kGlyphRepeatOne : kGlyphRepeatAll, HomeControl::Repeat,
                 model.repeatMode == RepeatMode::Off ? kIdleOpacity : 1, false);
    }

    context_->PopAxisAlignedClip();
    hr = context_->EndDraw();
    context_->SetTarget(nullptr);
    const HRESULT end = surface_->EndDraw();
    if (FAILED(hr)) return hr;
    return FAILED(end) ? end : compositor_->Commit();
}

}  // namespace noitch
