#pragma once
#include <windows.h>
#include <shellapi.h>
#include "shelf_model.h"
namespace noitch {
class ShelfSource {
public:
    void Initialize(HWND window);
    void SetRecording(bool recording);
    bool Recording() const { return recording_; }
    void Stop();
    bool Capture(ShelfItem* item, bool force = false);
    bool Copy(const ShelfItem& item);
    ShelfItem Files(const std::vector<std::string>& paths) const;
    void Load(std::vector<ShelfItem>& files, std::vector<ShelfItem>& clipboard);
    void Save(const std::vector<ShelfItem>& files, const std::vector<ShelfItem>& clipboard);
    std::vector<uint8_t> Payload(const ShelfItem& item) const;
    static std::vector<uint8_t> FilePayload(const std::vector<std::string>& paths);
    static std::vector<std::string> PathsFromDrop(HDROP drop);
private:
    std::wstring directory_;
    HWND window_ = nullptr;
    DWORD sequence_ = 0;
    UINT ownMarker_ = 0;
    bool recording_ = false;
    bool Store(const std::vector<uint8_t>& bytes, ShelfItem& item);
};
}
