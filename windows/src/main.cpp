#include <windows.h>
#include <ole2.h>

#include "island/island_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // OLE drag-and-drop uses the existing UI thread's STA, without a helper thread.
    const HRESULT ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 1;
    int result = 1;
    {
        noitch::IslandWindow island;
        if (SUCCEEDED(island.Create(instance))) {
            MSG message{};
            // GetMessage sleeps until an OS event arrives.
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                const HWND active = GetActiveWindow();
                if (active && IsDialogMessageW(active, &message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            result = static_cast<int>(message.wParam);
        }
    }
    OleUninitialize();
    return result;
}
