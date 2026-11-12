#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>
#include "features/home/home_layout.h"

namespace noitch {
enum class ShelfKind : uint32_t { Files, Text, Link, Image };
struct ShelfItem {
    std::string id;
    ShelfKind kind = ShelfKind::Text;
    std::string title;
    std::string preview;
    std::vector<std::string> files;
    std::string payload;
    bool pinned = false;
    bool operator==(const ShelfItem&) const = default;
};
inline constexpr size_t kShelfCapacity = 50;
inline bool InsertShelfItem(std::vector<ShelfItem>& items, ShelfItem item) {
    const auto previous = std::find_if(items.begin(), items.end(), [&](const auto& old) { return old.id == item.id; });
    if (previous != items.end()) { item.pinned = previous->pinned; items.erase(previous); }
    if (items.size() >= kShelfCapacity) {
        const auto expendable = std::find_if(items.rbegin(), items.rend(), [](const auto& old) { return !old.pinned; });
        if (expendable == items.rend()) return false;
        items.erase(std::next(expendable).base());
    }
    items.insert(items.begin(), std::move(item));
    return true;
}
class ShelfLayout {
public:
    ShelfLayout(float width = 580, float height = 180, float header = 32, float scale = 1) : width_(width), height_(height), header_(header), scale_(scale) {}
    HomeRect Card(int slot) const {
        const float width = (width_ - 60) / 3;
        return Px({22 + slot * (width + 8), header_ + 37, width, 77});
    }
    HomeRect Button(int index) const {
        const float widths[] = {59, 83, 60, 52, 28, 28};
        const float xs[] = {22, 83, width_ - 254, width_ - 190, width_ - 132, width_ - 100};
        return Px({xs[index], header_ + 7, widths[index], 23});
    }
    HomeRect Footer() const { return Px({24, height_ - 20, width_ - 48, 13}); }
    float Width() const { return width_ * scale_; }
    float Height() const { return height_ * scale_; }
    float Scale() const { return scale_; }
private:
    HomeRect Px(HomeRect r) const { return {r.x * scale_, r.y * scale_, r.width * scale_, r.height * scale_}; }
    float width_, height_, header_, scale_;
};
}
