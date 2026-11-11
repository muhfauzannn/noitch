#pragma once

#include <dcomp.h>
#include <windows.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <vector>

#include "island/compositor.h"
#include "island/painter.h"
#include "task_model.h"
#include "tasks_layout.h"

namespace noitch {

// What the slip says.
struct TaskSlip {
    TaskModule module = TaskModule::Assignment;
    std::string count;
    std::string lead;
    std::string detail;
    std::string text;
    bool urgent = false;
    bool operator==(const TaskSlip&) const = default;
};

// The closed island's task alert, drawn into one surface: the slip (an icon in the left wing, a count
// in the right and a line under the notch) or the dot that stays after it. It decides nothing. It
// is redrawn in place; the slide macOS does would need a visual per part. Direct2D loads only for
// the first alert.
class TaskAlertScene {
public:
    // `windowWidth` is the fixed window's width in pixels; the surface is centred on it.
    HRESULT Create(Compositor& compositor, const TaskAlertLayout& layout, int windowWidth, float scale);
    void Release();
    bool Exists() const { return root_ != nullptr; }

    // `slip` empty draws the dot, or nothing when `dot` is false. Commits.
    HRESULT Paint(const std::optional<TaskSlip>& slip, bool dot, bool liveShown);
    // Fades in after `delay` seconds, or out at once. Commits.
    HRESULT Show(bool visible, double delay, double now);

private:
    Compositor* compositor_ = nullptr;
    TaskAlertLayout layout_{0, 0};
    float scale_ = 1;
    float width_ = 0;
    float height_ = 0;
    ContentFade opacity_;
    Painter painter_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> root_;
    Microsoft::WRL::ComPtr<IDCompositionEffectGroup> effect_;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> stripFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> countFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> glyphFormat_;
};

// Owns the closed island's task alert: the slip for a few seconds when a task appears or a deadline
// nears, then a dot until the tasks page is seen. Driven by the watch's events and one one-shot
// timer for the slip's end; nothing runs while the dot shows. Matches TaskAlertController.swift.
class TaskAlertController {
public:
    static constexpr UINT_PTR kSlipTimer = 17;
    static constexpr UINT_PTR kReleaseTimer = 18;

    void Initialize(HWND window, Compositor* compositor, double (*now)(), bool enabled);
    void SetEnabled(bool enabled);
    void SetGeometry(float collapsedWidth, float collapsedHeight, int windowWidth, float scale);
    void SetLiveShown(bool shown);
    void SetUnviewed(bool unviewed);
    void Announce(const std::vector<MoodleTask>& tasks, double utcOffset);
    void Remind(const MoodleTask& task, double lead);
    void IslandWillOpen();
    void IslandWillClose();
    void DeviceLost();
    bool HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);

    TaskAlertShape Shape() const { return shape_; }
    // The tasks page is where the island should open while anything of this shows.
    bool WantsTasksPage() const { return shape_ != TaskAlertShape::None; }

    // Called when Shape() changes, so the island can resize the pill.
    void (*onShapeChange)(void* context) = nullptr;
    void* context = nullptr;

private:
    void Present(const TaskSlip& slip);
    void Evaluate(bool repaint = false);

    HWND window_ = nullptr;
    Compositor* compositor_ = nullptr;
    double (*now_)() = nullptr;
    bool enabled_ = true;
    bool hasGeometry_ = false;
    float collapsedWidth_ = 0;
    float collapsedHeight_ = 0;
    int windowWidth_ = 0;
    float scale_ = 1;
    bool liveShown_ = false;
    bool unviewed_ = false;
    bool islandOpen_ = false;
    std::optional<TaskSlip> slip_;
    // A reminder that came while another slip showed, or while the island was open.
    std::optional<TaskSlip> waiting_;
    TaskAlertShape shape_ = TaskAlertShape::None;
    TaskAlertScene scene_;
};

}  // namespace noitch
