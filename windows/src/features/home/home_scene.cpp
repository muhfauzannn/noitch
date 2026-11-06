#include "home_scene.h"

#include <d2d1_1helper.h>

#include <string>

using Microsoft::WRL::ComPtr;

namespace noitch {

namespace {

constexpr float kArtworkRadius = 14;
constexpr float kDimmedOpacity = 0.35f;
constexpr float kIdleOpacity = 0.55f;

// Ease-out throughout. Hover is a plate fading, and a swap is content fading back in from a low start.
constexpr double kPlateFadeSeconds = 0.1;
constexpr double kSwapFadeSeconds = 0.15;
constexpr float kSwapStartOpacity = 0.35f;
constexpr float kPlateHoverOpacity = 0.55f;
constexpr float kPlatePressedOpacity = 1.0f;
constexpr float kPlateAlpha = 0.22f;

// Segoe MDL2 Assets ships with Windows 10 and 11, so icons need no image files.
constexpr wchar_t kGlyphPlay[] = L"\xE768";
constexpr wchar_t kGlyphPause[] = L"\xE769";
constexpr wchar_t kGlyphPrevious[] = L"\xE892";
constexpr wchar_t kGlyphNext[] = L"\xE893";
constexpr wchar_t kGlyphShuffle[] = L"\xE8B1";
constexpr wchar_t kGlyphRepeatAll[] = L"\xE8EE";
constexpr wchar_t kGlyphRepeatOne[] = L"\xE8ED";
constexpr wchar_t kGlyphSettings[] = L"\xE713";
constexpr wchar_t kGlyphLyrics[] = L"\xE8BD";
constexpr wchar_t kGlyphMusic[] = L"\xEC4F";

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
    left_ = left;
    scale_ = scale;
    contentMotion_.Jump(1);
    plateMotion_.Jump(0);

    HRESULT hr;
    if (FAILED(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return hr;
    if (FAILED(hr = factory_->CreateDevice(compositor.DxgiDevice(), &device_))) return hr;
    if (FAILED(hr = device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_))) return hr;
    // Pixels are the unit everywhere here, so Direct2D must not scale again.
    context_->SetDpi(96, 96);
    if (FAILED(hr = context_->CreateSolidColorBrush(White(1), &brush_))) return hr;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())))) return hr;
    if (FAILED(hr = CreateFormats())) return hr;
    if (FAILED(hr = compositor.CreateContentSurface(static_cast<UINT>(layout.Width() + 0.5f), static_cast<UINT>(layout.Height() + 0.5f), left, &surface_))) return hr;
    // Optional. Without it there is simply no hover plate.
    plateSize_ = static_cast<int>(layout.Frame(HomeControl::PlayPause).width + 1.5f);
    if (FAILED(compositor.CreatePlateSurface(static_cast<UINT>(plateSize_), &plateSurface_))) plateSurface_.Reset();
    return S_OK;
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
    if (FAILED(hr = make(L"Segoe UI", 12, DWRITE_FONT_WEIGHT_NORMAL, left, &lyricFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL, left, &timeFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 11, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING, &remainingFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 13, DWRITE_FONT_WEIGHT_NORMAL, left, &panelFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe UI", 16, DWRITE_FONT_WEIGHT_SEMI_BOLD, left, &panelCurrentFormat_))) return hr;
    if (FAILED(hr = make(L"Segoe MDL2 Assets", 15, DWRITE_FONT_WEIGHT_NORMAL, center, &iconFormat_))) return hr;
    return make(L"Segoe MDL2 Assets", 21, DWRITE_FONT_WEIGHT_NORMAL, center, &largeIconFormat_);
}

void HomeScene::Release() {
    if (compositor_ && surface_) compositor_->RemoveContent();
    surface_.Reset();
    plateSurface_.Reset();
    plateDiameter_ = 0;
    artworkBrush_.Reset();
    titleFormat_.Reset();
    artistFormat_.Reset();
    lyricFormat_.Reset();
    timeFormat_.Reset();
    remainingFormat_.Reset();
    panelFormat_.Reset();
    panelCurrentFormat_.Reset();
    iconFormat_.Reset();
    largeIconFormat_.Reset();
    writeFactory_.Reset();
    brush_.Reset();
    context_.Reset();
    device_.Reset();
    factory_.Reset();
}

void HomeScene::SetArtwork(const std::shared_ptr<const Artwork>& artwork, uint32_t tint) {
    tint_ = tint;
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

HRESULT HomeScene::BeginDraw(IDCompositionSurface* surface) {
    ComPtr<IDXGISurface> dxgiSurface;
    POINT offset{};
    HRESULT hr = surface->BeginDraw(nullptr, IID_PPV_ARGS(&dxgiSurface), &offset);
    if (FAILED(hr)) return hr;

    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target;
    if (FAILED(hr = context_->CreateBitmapFromDxgiSurface(dxgiSurface.Get(), &properties, &target))) {
        surface->EndDraw();
        return hr;
    }
    context_->SetTarget(target.Get());
    context_->BeginDraw();
    // The surface can be a region of a shared atlas, so stay inside the area at `offset`.
    context_->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
    return S_OK;
}

HRESULT HomeScene::EndDraw(IDCompositionSurface* surface) {
    const HRESULT hr = context_->EndDraw();
    context_->SetTarget(nullptr);
    const HRESULT end = surface->EndDraw();
    return FAILED(hr) ? hr : end;
}

HRESULT HomeScene::DrawPlate(int diameter) {
    HRESULT hr;
    if (FAILED(hr = BeginDraw(plateSurface_.Get()))) return hr;
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    const float center = static_cast<float>(plateSize_) / 2;
    const float radius = static_cast<float>(diameter) / 2;
    brush_->SetColor(White(kPlateAlpha));
    context_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center, center), radius, radius), brush_.Get());
    plateDiameter_ = diameter;
    return EndDraw(plateSurface_.Get());
}

HRESULT HomeScene::SetHover(HomeControl control, bool isPressed, const HomeModel& model, double now) {
    if (!plateSurface_) return S_OK;
    const bool show = control != HomeControl::None && ControlHasPlate(control, model);
    if (show) {
        const HomeRect frame = layout_.Frame(control);
        const int diameter = static_cast<int>(frame.width + 0.5f);
        if (diameter != plateDiameter_ && FAILED(DrawPlate(diameter))) return E_FAIL;
        compositor_->SetPlateOffset(left_ + frame.x + frame.width / 2 - static_cast<float>(plateSize_) / 2,
                                    frame.y + frame.height / 2 - static_cast<float>(plateSize_) / 2);
    }
    const float target = show ? (isPressed ? kPlatePressedOpacity : kPlateHoverOpacity) : 0.0f;
    // A plate that only moved still needs the new position committed.
    if (target == plateMotion_.Target()) return show ? compositor_->Commit() : S_OK;
    plateMotion_.Retarget(target, kPlateFadeSeconds, now);
    return compositor_->AnimatePlateOpacity(plateMotion_);
}

HRESULT HomeScene::FadeInContent(double now) {
    contentMotion_.Start(kSwapStartOpacity, 1, kSwapFadeSeconds, now);
    return compositor_->AnimateContentOpacity(contentMotion_);
}

HRESULT HomeScene::Paint(const HomeModel& model) {
    if (!surface_) return S_OK;
    HRESULT hr;
    if (FAILED(hr = BeginDraw(surface_.Get()))) return hr;
    const D2D1_RECT_F whole = D2D1::RectF(0, 0, layout_.Width(), layout_.Height());
    context_->PushAxisAlignedClip(whole, D2D1_ANTIALIAS_MODE_ALIASED);
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const HomeRect art = layout_.Artwork();
    const float artRadius = kArtworkRadius * scale_;
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToD2D(art), artRadius, artRadius);
    if (artworkBrush_) {
        context_->FillRoundedRectangle(rounded, artworkBrush_.Get());
    } else {
        brush_->SetColor(White(0.08f));
        context_->FillRoundedRectangle(rounded, brush_.Get());
        brush_->SetColor(White(kDimmedOpacity));
        context_->DrawText(kGlyphMusic, 1, largeIconFormat_.Get(), ToD2D(art), brush_.Get());
    }

    if (model.showsLyricsPanel) {
        PaintText(model.panelLines[0], panelFormat_.Get(), layout_.PanelLine(0), 0.45f);
        PaintText(model.panelLines[1], panelCurrentFormat_.Get(), layout_.PanelLine(1), 1);
        PaintText(model.panelLines[2], panelFormat_.Get(), layout_.PanelLine(2), 0.45f);
    } else {
        PaintText(model.title, titleFormat_.Get(), layout_.Title(), 1);
        PaintText(model.artist, artistFormat_.Get(), layout_.Artist(), 0.6f);
        PaintText(model.lyricLine, lyricFormat_.Get(), layout_.LyricLine(), 0.8f);
        PaintText(model.elapsedText, timeFormat_.Get(), layout_.ElapsedTime(), 0.6f);
        PaintText(model.remainingText, remainingFormat_.Get(), layout_.RemainingTime(), 0.6f);

        const HomeRect track = layout_.SliderTrack();
        const float radius = track.height / 2;
        brush_->SetColor(White(0.2f));
        context_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(track), radius, radius), brush_.Get());
        HomeRect filled = track;
        filled.width = track.width * static_cast<float>(model.progress);
        brush_->SetColor(FromBgra(tint_));
        context_->FillRoundedRectangle(D2D1::RoundedRect(ToD2D(filled), radius, radius), brush_.Get());
    }

    const float transport = model.hasTrack ? 1 : kDimmedOpacity;
    PaintIcon(kGlyphSettings, HomeControl::Settings, 1, false);
    if (model.showsLyricsButton) PaintIcon(kGlyphLyrics, HomeControl::Lyrics, model.showsLyricsPanel ? 1 : kIdleOpacity, false);
    PaintIcon(kGlyphPrevious, HomeControl::Previous, transport, false);
    PaintIcon(model.isPlaying ? kGlyphPause : kGlyphPlay, HomeControl::PlayPause, transport, true);
    PaintIcon(kGlyphNext, HomeControl::Next, transport, false);
    if (model.showsShuffleAndRepeat) {
        PaintIcon(kGlyphShuffle, HomeControl::Shuffle, model.isShuffled ? 1 : kIdleOpacity, false);
        PaintIcon(model.repeatMode == RepeatMode::One ? kGlyphRepeatOne : kGlyphRepeatAll, HomeControl::Repeat,
                 model.repeatMode == RepeatMode::Off ? kIdleOpacity : 1, false);
    }

    context_->PopAxisAlignedClip();
    if (FAILED(hr = EndDraw(surface_.Get()))) return hr;
    return compositor_->Commit();
}

}  // namespace noitch
