#include "media_session_source.h"

#include <roapi.h>
#include <shcore.h>
#include <shellapi.h>
#include <wincodec.h>
#include <windows.foundation.h>
#include <windows.storage.streams.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <algorithm>
#include <memory>
#include <string>

using ABI::Windows::Foundation::AsyncStatus;
using ABI::Windows::Foundation::DateTime;
using ABI::Windows::Foundation::IAsyncOperation;
using ABI::Windows::Foundation::IAsyncOperationCompletedHandler;
using ABI::Windows::Foundation::IReference;
using ABI::Windows::Foundation::ITypedEventHandler;
using ABI::Windows::Foundation::TimeSpan;
using ABI::Windows::Media::MediaPlaybackAutoRepeatMode;
using ABI::Windows::Media::Control::CurrentSessionChangedEventArgs;
using ABI::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using ABI::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using ABI::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties;
using ABI::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionManagerStatics;
using ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionMediaProperties;
using ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionPlaybackInfo;
using ABI::Windows::Media::Control::IGlobalSystemMediaTransportControlsSessionTimelineProperties;
using ABI::Windows::Media::Control::MediaPropertiesChangedEventArgs;
using ABI::Windows::Media::Control::PlaybackInfoChangedEventArgs;
using ABI::Windows::Media::Control::TimelinePropertiesChangedEventArgs;
using ABI::Windows::Storage::Streams::IRandomAccessStreamReference;
using ABI::Windows::Storage::Streams::IRandomAccessStreamWithContentType;
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;

namespace noitch {

namespace {

// What kMessage's wparam says arrived. The low bits carry the kind, the rest a generation.
enum Kind : WPARAM {
    kManagerReady,   // lparam: a Manager*, with a reference to take over
    kSessionChanged,
    kMediaChanged,
    kPlaybackChanged,
    kMediaRead,      // lparam: a MediaResult*, to delete
    kArtworkRead,    // lparam: an ArtworkResult*, to delete
};

struct MediaResult {
    uint32_t generation;
    std::string title;
    std::string artist;
    std::string album;
};

struct ArtworkResult {
    uint32_t generation;
    std::shared_ptr<const Artwork> artwork;
};

constexpr double kTicksPerSecond = 10'000'000.0;

std::string ToUtf8(HSTRING text) {
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(text, &length);
    if (!raw || length == 0) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, raw, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, raw, static_cast<int>(length), result.data(), bytes, nullptr, nullptr);
    return result;
}

// Crops to a centered square, scales to `size`, and converts to premultiplied BGRA.
std::shared_ptr<const Artwork> DecodeArtwork(IStream* stream, int size) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return nullptr;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))) return nullptr;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;
    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) return nullptr;

    const UINT side = std::min(width, height);
    const WICRect square{static_cast<INT>((width - side) / 2), static_cast<INT>((height - side) / 2), static_cast<INT>(side), static_cast<INT>(side)};
    ComPtr<IWICBitmapClipper> clipper;
    if (FAILED(factory->CreateBitmapClipper(&clipper)) || FAILED(clipper->Initialize(frame.Get(), &square))) return nullptr;
    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler))) return nullptr;
    if (FAILED(scaler->Initialize(clipper.Get(), static_cast<UINT>(size), static_cast<UINT>(size), WICBitmapInterpolationModeFant))) return nullptr;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return nullptr;
    if (FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return nullptr;

    auto artwork = std::make_shared<Artwork>();
    artwork->size = size;
    artwork->pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size));
    const UINT stride = static_cast<UINT>(size) * 4;
    if (FAILED(converter->CopyPixels(nullptr, stride, stride * static_cast<UINT>(size), reinterpret_cast<BYTE*>(artwork->pixels.data())))) return nullptr;
    return artwork;
}

// Runs on a pool thread, where WinRT's completion arrives, and ends by posting to the main thread.
void ReadThumbnail(IRandomAccessStreamReference* thumbnail, HWND notify, uint32_t generation, int size) {
    const auto post = [=](std::shared_ptr<const Artwork> artwork) {
        auto result = std::make_unique<ArtworkResult>(ArtworkResult{generation, std::move(artwork)});
        if (PostMessageW(notify, MediaSessionSource::kMessage, kArtworkRead, reinterpret_cast<LPARAM>(result.get()))) result.release();
    };
    if (!thumbnail) {
        post(nullptr);
        return;
    }
    ComPtr<IAsyncOperation<IRandomAccessStreamWithContentType*>> open;
    if (FAILED(thumbnail->OpenReadAsync(&open))) {
        post(nullptr);
        return;
    }
    open->put_Completed(Callback<IAsyncOperationCompletedHandler<IRandomAccessStreamWithContentType*>>(
        [post, size](IAsyncOperation<IRandomAccessStreamWithContentType*>* operation, AsyncStatus status) -> HRESULT {
            std::shared_ptr<const Artwork> artwork;
            ComPtr<IRandomAccessStreamWithContentType> stream;
            if (status == AsyncStatus::Completed && SUCCEEDED(operation->GetResults(&stream)) && stream) {
                const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                ComPtr<IStream> classic;
                if (SUCCEEDED(CreateStreamOverRandomAccessStream(stream.Get(), IID_PPV_ARGS(&classic)))) {
                    artwork = DecodeArtwork(classic.Get(), size);
                }
                if (SUCCEEDED(com)) CoUninitialize();
            }
            post(std::move(artwork));
            return S_OK;
        }).Get());
}

// A fresh handler per event, posting `kind` to the main thread. Events arrive on pool threads.
template <typename Handler>
ComPtr<Handler> MakePoster(HWND window, WPARAM kind) {
    return Callback<Handler>([window, kind](auto*, auto*) -> HRESULT {
        PostMessageW(window, MediaSessionSource::kMessage, kind, 0);
        return S_OK;
    });
}

}  // namespace

void MediaSessionSource::Start(HWND notify, double (*now)()) {
    if (running_) return;
    notify_ = notify;
    now_ = now;
    if (!roInitialized_) {
        // RPC_E_CHANGED_MODE would still leave WinRT usable, so only success is remembered as such.
        roInitialized_ = SUCCEEDED(RoInitialize(RO_INIT_MULTITHREADED));
    }
    ComPtr<IGlobalSystemMediaTransportControlsSessionManagerStatics> statics;
    const HStringReference name(RuntimeClass_Windows_Media_Control_GlobalSystemMediaTransportControlsSessionManager);
    if (FAILED(RoGetActivationFactory(name.Get(), IID_PPV_ARGS(&statics)))) return;
    ComPtr<IAsyncOperation<GlobalSystemMediaTransportControlsSessionManager*>> request;
    if (FAILED(statics->RequestAsync(&request))) return;
    running_ = true;
    const HWND window = notify_;
    request->put_Completed(Callback<IAsyncOperationCompletedHandler<GlobalSystemMediaTransportControlsSessionManager*>>(
        [window](IAsyncOperation<GlobalSystemMediaTransportControlsSessionManager*>* operation, AsyncStatus status) -> HRESULT {
            ComPtr<Manager> manager;
            if (status == AsyncStatus::Completed && SUCCEEDED(operation->GetResults(&manager)) && manager) {
                if (PostMessageW(window, kMessage, kManagerReady, reinterpret_cast<LPARAM>(manager.Get()))) manager.Detach();
            }
            return S_OK;
        }).Get());
}

void MediaSessionSource::Stop() {
    if (!running_) return;
    running_ = false;
    ++generation_;
    UnbindSession();
    if (manager_) manager_->remove_CurrentSessionChanged(sessionChangedToken_);
    manager_.Reset();
    state_ = NowPlaying{};
    artwork_.reset();
}

bool MediaSessionSource::HandleMessage(WPARAM kind, LPARAM payload) {
    switch (kind) {
        case kManagerReady: {
            ComPtr<Manager> manager;
            manager.Attach(reinterpret_cast<Manager*>(payload));
            // A stop and start can leave two requests in flight. One manager is enough.
            if (!running_ || manager_) return false;
            manager_ = manager;
            using Handler = ITypedEventHandler<GlobalSystemMediaTransportControlsSessionManager*, CurrentSessionChangedEventArgs*>;
            manager_->add_CurrentSessionChanged(MakePoster<Handler>(notify_, kSessionChanged).Get(), &sessionChangedToken_);
            BindSession();
            return true;
        }
        case kSessionChanged:
            if (!running_) return false;
            BindSession();
            return true;
        case kMediaChanged:
            if (!running_) return false;
            RequestMediaProperties();
            return false;
        case kPlaybackChanged:
            if (!running_) return false;
            ReadTimelineAndPlayback();
            return true;
        case kMediaRead: {
            const std::unique_ptr<MediaResult> result(reinterpret_cast<MediaResult*>(payload));
            if (result->generation != generation_) return false;
            state_.title = result->title;
            state_.artist = result->artist;
            state_.album = result->album;
            ReadTimelineAndPlayback();
            return true;
        }
        case kArtworkRead: {
            const std::unique_ptr<ArtworkResult> result(reinterpret_cast<ArtworkResult*>(payload));
            if (result->generation != generation_) return false;
            artwork_ = result->artwork;
            return true;
        }
        default:
            return false;
    }
}

void MediaSessionSource::BindSession() {
    UnbindSession();
    state_ = NowPlaying{};
    artwork_.reset();
    if (!manager_ || FAILED(manager_->GetCurrentSession(&session_)) || !session_) return;
    using MediaHandler = ITypedEventHandler<GlobalSystemMediaTransportControlsSession*, MediaPropertiesChangedEventArgs*>;
    using TimelineHandler = ITypedEventHandler<GlobalSystemMediaTransportControlsSession*, TimelinePropertiesChangedEventArgs*>;
    using PlaybackHandler = ITypedEventHandler<GlobalSystemMediaTransportControlsSession*, PlaybackInfoChangedEventArgs*>;
    session_->add_MediaPropertiesChanged(MakePoster<MediaHandler>(notify_, kMediaChanged).Get(), &mediaToken_);
    session_->add_TimelinePropertiesChanged(MakePoster<TimelineHandler>(notify_, kPlaybackChanged).Get(), &timelineToken_);
    session_->add_PlaybackInfoChanged(MakePoster<PlaybackHandler>(notify_, kPlaybackChanged).Get(), &playbackToken_);
    RequestMediaProperties();
    ReadTimelineAndPlayback();
}

void MediaSessionSource::UnbindSession() {
    if (!session_) return;
    session_->remove_MediaPropertiesChanged(mediaToken_);
    session_->remove_TimelinePropertiesChanged(timelineToken_);
    session_->remove_PlaybackInfoChanged(playbackToken_);
    session_.Reset();
}

void MediaSessionSource::RequestMediaProperties() {
    if (!session_) return;
    const uint32_t generation = ++generation_;
    ComPtr<IAsyncOperation<GlobalSystemMediaTransportControlsSessionMediaProperties*>> request;
    if (FAILED(session_->TryGetMediaPropertiesAsync(&request))) return;
    const HWND window = notify_;
    const int size = artworkPixels_;
    request->put_Completed(Callback<IAsyncOperationCompletedHandler<GlobalSystemMediaTransportControlsSessionMediaProperties*>>(
        [window, generation, size](IAsyncOperation<GlobalSystemMediaTransportControlsSessionMediaProperties*>* operation, AsyncStatus status) -> HRESULT {
            ComPtr<IGlobalSystemMediaTransportControlsSessionMediaProperties> properties;
            if (status != AsyncStatus::Completed || FAILED(operation->GetResults(&properties)) || !properties) return S_OK;
            auto result = std::make_unique<MediaResult>(MediaResult{generation, {}, {}, {}});
            HString title, artist, album;
            if (SUCCEEDED(properties->get_Title(title.GetAddressOf()))) result->title = ToUtf8(title.Get());
            if (SUCCEEDED(properties->get_Artist(artist.GetAddressOf()))) result->artist = ToUtf8(artist.Get());
            if (SUCCEEDED(properties->get_AlbumTitle(album.GetAddressOf()))) result->album = ToUtf8(album.Get());
            if (PostMessageW(window, kMessage, kMediaRead, reinterpret_cast<LPARAM>(result.get()))) result.release();
            ComPtr<IRandomAccessStreamReference> thumbnail;
            properties->get_Thumbnail(&thumbnail);
            ReadThumbnail(thumbnail.Get(), window, generation, size);
            return S_OK;
        }).Get());
}

void MediaSessionSource::ReadTimelineAndPlayback() {
    if (!session_) return;
    ComPtr<IGlobalSystemMediaTransportControlsSessionPlaybackInfo> info;
    if (SUCCEEDED(session_->GetPlaybackInfo(&info)) && info) {
        GlobalSystemMediaTransportControlsSessionPlaybackStatus status{};
        if (SUCCEEDED(info->get_PlaybackStatus(&status))) {
            state_.isPlaying = status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::GlobalSystemMediaTransportControlsSessionPlaybackStatus_Playing;
        }
        ComPtr<IReference<double>> rate;
        double rateValue = 1;
        if (SUCCEEDED(info->get_PlaybackRate(&rate)) && rate && SUCCEEDED(rate->get_Value(&rateValue)) && rateValue > 0) state_.rate = rateValue;
        ComPtr<IReference<bool>> shuffle;
        boolean shuffleValue = false;
        if (SUCCEEDED(info->get_IsShuffleActive(&shuffle)) && shuffle && SUCCEEDED(shuffle->get_Value(&shuffleValue))) state_.isShuffled = shuffleValue != 0;
        ComPtr<IReference<MediaPlaybackAutoRepeatMode>> repeat;
        MediaPlaybackAutoRepeatMode repeatValue{};
        if (SUCCEEDED(info->get_AutoRepeatMode(&repeat)) && repeat && SUCCEEDED(repeat->get_Value(&repeatValue))) {
            state_.repeatMode = repeatValue == MediaPlaybackAutoRepeatMode::MediaPlaybackAutoRepeatMode_Track ? RepeatMode::One
                              : repeatValue == MediaPlaybackAutoRepeatMode::MediaPlaybackAutoRepeatMode_List ? RepeatMode::All
                                                                                                            : RepeatMode::Off;
        }
    }

    ComPtr<IGlobalSystemMediaTransportControlsSessionTimelineProperties> timeline;
    if (SUCCEEDED(session_->GetTimelineProperties(&timeline)) && timeline) {
        TimeSpan position{};
        TimeSpan end{};
        DateTime updated{};
        timeline->get_Position(&position);
        timeline->get_EndTime(&end);
        timeline->get_LastUpdatedTime(&updated);
        state_.duration = static_cast<double>(end.Duration) / kTicksPerSecond;
        state_.elapsed = static_cast<double>(position.Duration) / kTicksPerSecond;
        if (state_.isPlaying && updated.UniversalTime > 0) {
            // The session reports where it was when it last spoke, which can be a while ago.
            FILETIME nowFile;
            GetSystemTimeAsFileTime(&nowFile);
            const auto nowTicks = static_cast<int64_t>(static_cast<uint64_t>(nowFile.dwHighDateTime) << 32 | nowFile.dwLowDateTime);
            state_.elapsed += static_cast<double>(std::max<int64_t>(nowTicks - updated.UniversalTime, 0)) / kTicksPerSecond * state_.rate;
        }
        state_.timestamp = now_();
    }
}

void MediaSessionSource::OpenApp() {
    if (!session_) return;
    HString id;
    if (FAILED(session_->get_SourceAppUserModelId(id.GetAddressOf()))) return;
    UINT32 length = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(id.Get(), &length);
    if (!raw || length == 0) return;
    // Store apps open this way. A desktop player reports an exe name, which simply does nothing.
    const std::wstring target = L"shell:AppsFolder\\" + std::wstring(raw, length);
    ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void MediaSessionSource::TogglePlayPause() {
    ComPtr<IAsyncOperation<bool>> ignored;
    if (session_) session_->TryTogglePlayPauseAsync(&ignored);
}

void MediaSessionSource::Next() {
    ComPtr<IAsyncOperation<bool>> ignored;
    if (session_) session_->TrySkipNextAsync(&ignored);
}

void MediaSessionSource::Previous() {
    ComPtr<IAsyncOperation<bool>> ignored;
    if (session_) session_->TrySkipPreviousAsync(&ignored);
}

void MediaSessionSource::Seek(double seconds) {
    ComPtr<IAsyncOperation<bool>> ignored;
    if (session_) session_->TryChangePlaybackPositionAsync(static_cast<INT64>(seconds * kTicksPerSecond), &ignored);
}

void MediaSessionSource::SetShuffle(bool on) {
    ComPtr<IAsyncOperation<bool>> ignored;
    if (session_) session_->TryChangeShuffleActiveAsync(on, &ignored);
}

void MediaSessionSource::SetRepeat(RepeatMode mode) {
    ComPtr<IAsyncOperation<bool>> ignored;
    const MediaPlaybackAutoRepeatMode value = mode == RepeatMode::One ? MediaPlaybackAutoRepeatMode::MediaPlaybackAutoRepeatMode_Track
                                            : mode == RepeatMode::All ? MediaPlaybackAutoRepeatMode::MediaPlaybackAutoRepeatMode_List
                                                                      : MediaPlaybackAutoRepeatMode::MediaPlaybackAutoRepeatMode_None;
    if (session_) session_->TryChangeAutoRepeatModeAsync(value, &ignored);
}

}  // namespace noitch
