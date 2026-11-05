#include <windows.h>

#include "island/island_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    noitch::IslandWindow island;
    if (FAILED(island.Create(instance))) return 1;

    // GetMessage blocks in the kernel: at rest this thread is not scheduled at all.
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Tab and Space in the settings window. The island never activates, so this only sees it.
        const HWND active = GetActiveWindow();
        if (active && IsDialogMessageW(active, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
