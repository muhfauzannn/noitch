#pragma once

#include <dwrite.h>
#include <wrl/client.h>

#include "island/compositor.h"
#include "island/painter.h"
#include "task_model.h"
#include "tasks_layout.h"

namespace noitch {

// Draws the tasks page into one surface: the next task as a countdown, a runway of the next two weeks
// with a dot per task, and a short list. It decides nothing, and draws only when the model or the
// hovered row changes. Windows draws the countdown in one size, and the course after the title in
// the same colour; macOS sets each in its own style.
class TasksScene {
public:
    HRESULT Create(Compositor& compositor, Painter& painter, const TasksLayout& layout, float left, float scale);
    void Release();
    bool Exists() const { return surface_ != nullptr; }
    const TasksLayout& Layout() const { return layout_; }

    HRESULT Paint(const TasksPageModel& model, int hoveredRow);

private:
    Compositor* compositor_ = nullptr;
    Painter* painter_ = nullptr;
    TasksLayout layout_;
    TasksPageModel shown_;
    int shownHover_ = -1;
    bool hasShown_ = false;
    Microsoft::WRL::ComPtr<IDCompositionSurface> surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> countdownFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> chipFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> buttonFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dayFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> rowFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> dueFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> toggleFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> messageFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> detailFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> checkFormat_;
};

// Shared by the page and the closed island's alert.
namespace task_colors {

inline constexpr D2D1_COLOR_F kCritical = {1.0f, 0.35f, 0.31f, 1};
inline constexpr D2D1_COLOR_F kWatch = {1.0f, 0.71f, 0.28f, 1};
inline constexpr D2D1_COLOR_F kFresh = {0.3f, 0.55f, 1.0f, 1};

inline D2D1_COLOR_F For(Severity severity) {
    return severity == Severity::Critical ? kCritical : severity == Severity::Watch ? kWatch : D2D1_COLOR_F{1, 1, 1, 1};
}

// Segoe MDL2 Assets: a page, a tick and a speech bubble.
inline const wchar_t* Glyph(TaskModule module) {
    switch (module) {
        case TaskModule::Assignment: return L"\xE8A5";
        case TaskModule::Quiz: return L"\xE73E";
        default: return L"\xE8BD";
    }
}

}  // namespace task_colors

}  // namespace noitch
