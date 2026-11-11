#include "task_alert.h"

#include <cmath>
#include <set>

#include "tasks_scene.h"
#include "task_watch.h"

namespace noitch {

using Microsoft::WRL::ComPtr;

namespace {

D2D1_COLOR_F White(float opacity) { return D2D1::ColorF(1, 1, 1, opacity); }

}  // namespace

HRESULT TaskAlertScene::Create(Compositor& compositor, const TaskAlertLayout& layout, int windowWidth, float scale) {
    Release();
    compositor_ = &compositor;
    layout_ = layout;
    scale_ = scale;
    width_ = std::ceil(layout.Width() * scale);
    height_ = std::ceil(layout.Height() * scale);
    IDCompositionDevice* device = compositor.Device();
    if (!device) return E_UNEXPECTED;
    HRESULT hr;
    if (FAILED(hr = painter_.Create(compositor.DxgiDevice()))) return hr;
    if (FAILED(hr = painter_.MakeFormat(L"Segoe UI", LiveLayout::kLyricFontSize * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, &stripFormat_))) return hr;
    if (FAILED(hr = painter_.MakeFormat(L"Segoe UI", 12 * scale, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, &countFormat_))) return hr;
    if (FAILED(hr = painter_.MakeFormat(L"Segoe MDL2 Assets", 11 * scale, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, &glyphFormat_))) return hr;
    if (FAILED(hr = device->CreateVisual(&root_))) return hr;
    if (FAILED(hr = device->CreateEffectGroup(&effect_))) return hr;
    if (FAILED(hr = effect_->SetOpacity(0.0f))) return hr;
    opacity_ = {0, 0, 0, 0, 0};
    if (FAILED(hr = root_->SetEffect(effect_.Get()))) return hr;
    if (FAILED(hr = device->CreateSurface(static_cast<UINT>(width_), static_cast<UINT>(height_), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface_))) return hr;
    if (FAILED(hr = root_->SetContent(surface_.Get()))) return hr;
    if (FAILED(hr = root_->SetOffsetX(std::round((windowWidth - width_) / 2)))) return hr;
    if (FAILED(hr = root_->SetOffsetY(0.0f))) return hr;
    return compositor.AttachLive(root_.Get());
}

void TaskAlertScene::Release() {
    if (compositor_ && root_) compositor_->DetachLive(root_.Get());
    surface_.Reset();
    effect_.Reset();
    root_.Reset();
    stripFormat_.Reset();
    countFormat_.Reset();
    glyphFormat_.Reset();
    painter_.Release();
    if (compositor_) compositor_->Commit();
}

HRESULT TaskAlertScene::Paint(const std::optional<TaskSlip>& slip, bool dot, bool liveShown) {
    if (!root_) return S_OK;
    HRESULT hr = painter_.Begin(surface_.Get(), width_, height_);
    if (FAILED(hr)) return hr;
    const auto px = [this](HomeRect r) { return HomeRect{r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; };
    if (slip) {
        const D2D1_COLOR_F accent = slip->urgent ? task_colors::kCritical : task_colors::kFresh;
        if (!liveShown) {
            // The music's wings keep their artwork and bars; without music, the slip has its own.
            const HomeRect glyph = px(layout_.Glyph());
            const D2D1_COLOR_F plate = slip->urgent || slip->module == TaskModule::Quiz ? task_colors::kCritical : task_colors::kWatch;
            painter_.Fill(glyph, 5 * scale_, plate);
            painter_.Glyph(task_colors::Glyph(slip->module), glyphFormat_.Get(), {glyph.x, glyph.y + 3 * scale_, glyph.width, glyph.height - 3 * scale_},
                           D2D1::ColorF(0, 0, 0, 1));
            painter_.Text(slip->count, countFormat_.Get(), px(layout_.Count()), accent);
        }
        // In one colour: per-run colours would need a DirectWrite text layout. macOS colours the lead.
        std::string line = slip->lead + "  " + (slip->detail.empty() ? "" : slip->detail + "  ") + slip->text;
        painter_.Text(line, stripFormat_.Get(), px(layout_.Strip()), White(1));
    } else if (dot) {
        const HomeRect rect = px(layout_.Dot(liveShown));
        painter_.Dot({rect.x + rect.width / 2, rect.y + rect.height / 2}, rect.width / 2, task_colors::kFresh);
    }
    if (FAILED(hr = painter_.End(surface_.Get()))) return hr;
    return compositor_->Commit();
}

HRESULT TaskAlertScene::Show(bool visible, double delay, double now) {
    if (!root_) return S_OK;
    const float from = static_cast<float>(opacity_.ValueAt(now));
    const float to = visible ? 1.0f : 0.0f;
    if (from == to && opacity_.to == to) return S_OK;
    opacity_ = {now, visible ? delay : 0, visible ? 0.3 : 0.12, from, to};
    ComPtr<IDCompositionAnimation> fade;
    HRESULT hr;
    if (FAILED(hr = compositor_->BuildFade(opacity_, &fade))) return hr;
    if (FAILED(hr = effect_->SetOpacity(fade.Get()))) return hr;
    return compositor_->Commit();
}

void TaskAlertController::Initialize(HWND window, Compositor* compositor, double (*now)(), bool enabled) {
    window_ = window;
    compositor_ = compositor;
    now_ = now;
    enabled_ = enabled;
}

void TaskAlertController::SetEnabled(bool enabled) {
    if (enabled == enabled_) return;
    enabled_ = enabled;
    if (!enabled) {
        slip_.reset();
        waiting_.reset();
        KillTimer(window_, kSlipTimer);
    }
    Evaluate();
}

void TaskAlertController::SetGeometry(float collapsedWidth, float collapsedHeight, int windowWidth, float scale) {
    if (hasGeometry_ && collapsedWidth == collapsedWidth_ && collapsedHeight == collapsedHeight_ && windowWidth == windowWidth_ && scale == scale_) return;
    hasGeometry_ = true;
    collapsedWidth_ = collapsedWidth;
    collapsedHeight_ = collapsedHeight;
    windowWidth_ = windowWidth;
    scale_ = scale;
    scene_.Release();
    Evaluate(true);
}

void TaskAlertController::SetLiveShown(bool shown) {
    if (shown == liveShown_) return;
    liveShown_ = shown;
    Evaluate(true);
}

void TaskAlertController::SetUnviewed(bool unviewed) {
    if (unviewed == unviewed_) return;
    unviewed_ = unviewed;
    Evaluate();
}

void TaskAlertController::Announce(const std::vector<MoodleTask>& tasks, double utcOffset) {
    // The page shows them anyway; the dot waits for the island to close.
    if (!enabled_ || islandOpen_ || tasks.empty()) return;
    TaskSlip slip;
    slip.module = tasks.front().module;
    if (tasks.size() == 1) {
        slip.count = "+1";
        slip.lead = "New";
        slip.detail = TaskRule::DueText(tasks.front().due, TaskNow(), utcOffset);
        slip.text = tasks.front().course + " \xC2\xB7 " + tasks.front().title;
    } else {
        slip.count = "+" + std::to_string(tasks.size());
        slip.lead = std::to_string(tasks.size()) + " new tasks";
        std::set<std::string> listed;
        for (const MoodleTask& task : tasks) {
            if (listed.size() == 3 || !listed.insert(task.course).second) continue;
            slip.text += (slip.text.empty() ? "" : ", ") + task.course;
        }
    }
    Present(slip);
}

void TaskAlertController::Remind(const MoodleTask& task, double lead) {
    if (!enabled_) return;
    TaskSlip slip;
    slip.module = task.module;
    slip.count = lead >= 86400 ? "1d" : "1h";
    slip.lead = lead >= 86400 ? "Due tomorrow" : "Due in 1 hour";
    slip.detail = task.course;
    slip.text = task.title;
    slip.urgent = true;
    if (islandOpen_ || slip_) {
        waiting_ = slip;
    } else {
        Present(slip);
    }
}

void TaskAlertController::IslandWillOpen() {
    islandOpen_ = true;
    // Opening is the look the slip asked for; it doesn't come back afterwards.
    slip_.reset();
    KillTimer(window_, kSlipTimer);
    scene_.Show(false, 0, now_());
    Evaluate();
}

void TaskAlertController::IslandWillClose() {
    islandOpen_ = false;
    if (waiting_) {
        const TaskSlip slip = *waiting_;
        waiting_.reset();
        Present(slip);
    } else {
        Evaluate(true);
    }
    // After the pill has narrowed most of the way back.
    scene_.Show(true, 0.22, now_());
}

void TaskAlertController::DeviceLost() {
    scene_.Release();
    Evaluate(true);
}

void TaskAlertController::Present(const TaskSlip& slip) {
    slip_ = slip;
    KillTimer(window_, kSlipTimer);
    // One-shot, not periodic: the slip's end.
    SetTimer(window_, kSlipTimer, static_cast<UINT>(TaskAlertLayout::kSlipDuration * 1000), nullptr);
    Evaluate(true);
}

void TaskAlertController::Evaluate(bool repaint) {
    const TaskAlertShape target = !enabled_ || !hasGeometry_ ? TaskAlertShape::None
                                  : slip_ && !islandOpen_    ? TaskAlertShape::Slip
                                  : unviewed_                ? TaskAlertShape::Dot
                                                             : TaskAlertShape::None;
    const bool changed = target != shape_;
    shape_ = target;
    if (target != TaskAlertShape::None) {
        KillTimer(window_, kReleaseTimer);
        if (!scene_.Exists()) {
            if (FAILED(scene_.Create(*compositor_, TaskAlertLayout(collapsedWidth_, collapsedHeight_), windowWidth_, scale_))) {
                scene_.Release();
            } else {
                repaint = true;
                // Waits for the pill to widen.
                if (!islandOpen_) scene_.Show(true, 0.12, now_());
            }
        }
    }
    if (changed || repaint) {
        scene_.Paint(target == TaskAlertShape::Slip ? slip_ : std::nullopt, target == TaskAlertShape::Dot, liveShown_);
    }
    if (target == TaskAlertShape::None && scene_.Exists()) {
        // One-shot: nothing of the alert outlives it by more than two seconds.
        SetTimer(window_, kReleaseTimer, 2000, nullptr);
    }
    if (changed && onShapeChange) onShapeChange(context);
}

bool TaskAlertController::HandleMessage(UINT message, WPARAM wparam, LPARAM) {
    if (message != WM_TIMER) return false;
    if (wparam == kSlipTimer) {
        KillTimer(window_, kSlipTimer);
        slip_.reset();
        if (waiting_) {
            const TaskSlip slip = *waiting_;
            waiting_.reset();
            Present(slip);
        } else {
            Evaluate();
        }
        return true;
    }
    if (wparam == kReleaseTimer) {
        KillTimer(window_, kReleaseTimer);
        if (shape_ == TaskAlertShape::None) scene_.Release();
        return true;
    }
    return false;
}

}  // namespace noitch
