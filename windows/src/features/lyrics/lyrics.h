#pragma once

// Parsing and matching for lyrics. No Windows headers, so it is unit-testable on any host. Windows
// has no JSON parser short of WinRT, and LRCLIB needs four fields, so a small reader lives here.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace noitch {

struct LyricLine {
    double start = 0;
    std::string text;
    std::string romanization;
};

struct Lyrics {
    // Sorted by start time. Empty if only plain lyrics exist.
    std::vector<LyricLine> synced;
    std::vector<std::string> plain;
    bool isInstrumental = false;

    bool Empty() const { return synced.empty() && plain.empty() && !isInstrumental; }

    // The line being sung at `position`, or -1 before the first line.
    int LineIndex(double position) const {
        const auto next = std::upper_bound(synced.begin(), synced.end(), position,
                                           [](double value, const LyricLine& line) { return value < line.start; });
        return static_cast<int>(next - synced.begin()) - 1;
    }

    // When the visible line next changes, so one one-shot timer can replace polling. -1 if never.
    double NextChange(double position) const {
        const size_t next = static_cast<size_t>(LineIndex(position) + 1);
        return next < synced.size() ? synced[next].start : -1;
    }
};

inline std::string_view TrimSpaces(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// "mm:ss.xx", or nothing for a metadata tag such as "ar:Name".
inline std::optional<double> ParseLrcTimestamp(std::string_view tag) {
    const size_t colon = tag.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == tag.size()) return std::nullopt;
    double parts[2] = {0, 0};
    const std::string_view fields[2] = {tag.substr(0, colon), tag.substr(colon + 1)};
    for (int i = 0; i < 2; ++i) {
        const std::string field(fields[i]);
        char* end = nullptr;
        parts[i] = std::strtod(field.c_str(), &end);
        if (end != field.c_str() + field.size()) return std::nullopt;
    }
    return parts[0] * 60 + parts[1];
}

// A line may carry several timestamps. Metadata tags fall out, since they aren't timestamps.
inline std::vector<LyricLine> ParseLrc(std::string_view text) {
    std::vector<LyricLine> lines;
    while (!text.empty()) {
        const size_t newline = text.find('\n');
        std::string_view rest = text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
        std::vector<double> starts;
        while (!rest.empty() && rest.front() == '[') {
            const size_t close = rest.find(']');
            if (close == std::string_view::npos) break;
            const auto time = ParseLrcTimestamp(rest.substr(1, close - 1));
            if (!time) break;
            starts.push_back(*time);
            rest.remove_prefix(close + 1);
        }
        for (const double start : starts) lines.push_back({start, std::string(TrimSpaces(rest))});
    }
    std::stable_sort(lines.begin(), lines.end(), [](const LyricLine& a, const LyricLine& b) { return a.start < b.start; });
    return lines;
}

inline bool IsTitleNoise(std::string_view text) {
    std::string lowered(text);
    for (char& c : lowered) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    for (const char* word : {"feat", "ft.", "with ", "remaster", "live", "official", "lyric", "video", "audio", "version", "mono", "stereo", "deluxe", "edit"}) {
        if (lowered.find(word) != std::string::npos) return true;
    }
    return false;
}

// Removes what a player adds to a title but a lyrics database doesn't have, such as "(feat. Someone)"
// or " - Remastered 2011". Byte-wise, which is safe: every character it looks for is ASCII.
inline std::string CleanTitle(std::string_view title) {
    std::string result;
    std::string group;
    int depth = 0;
    for (const char c : title) {
        if (c == '(' || c == '[') {
            if (depth > 0) group.push_back(c);
            ++depth;
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
            if (depth == 0) {
                if (!IsTitleNoise(group)) result += "(" + group + ")";
                group.clear();
            } else {
                group.push_back(c);
            }
        } else if (depth > 0) {
            group.push_back(c);
        } else {
            result.push_back(c);
        }
    }
    const size_t dash = result.rfind(" - ");
    if (dash != std::string::npos && IsTitleNoise(std::string_view(result).substr(dash + 3))) result.resize(dash);
    std::string collapsed;
    for (const char c : result) {
        if (c == ' ' && (collapsed.empty() || collapsed.back() == ' ')) continue;
        collapsed.push_back(c);
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    return collapsed;
}

// An LRCLIB result.
struct LyricsRecord {
    double duration = 0;
    bool instrumental = false;
    std::string plainLyrics;
    std::string syncedLyrics;

    Lyrics ToLyrics() const {
        Lyrics lyrics;
        lyrics.synced = ParseLrc(syncedLyrics);
        lyrics.isInstrumental = instrumental;
        std::string_view rest = plainLyrics;
        while (!rest.empty()) {
            const size_t newline = rest.find('\n');
            const std::string_view line = TrimSpaces(rest.substr(0, newline));
            if (!line.empty()) lyrics.plain.emplace_back(line);
            rest = newline == std::string_view::npos ? std::string_view{} : rest.substr(newline + 1);
        }
        return lyrics;
    }
};

// A longer difference than this is probably another recording of the song.
inline constexpr double kMaxLyricsDurationMismatch = 15;

// Synced lyrics beat plain ones; among those, the closest length wins.
inline std::optional<LyricsRecord> BestLyricsRecord(const std::vector<LyricsRecord>& records, double duration) {
    const auto mismatch = [&](const LyricsRecord& record) {
        return duration > 0 && record.duration > 0 ? std::fabs(record.duration - duration) : 0.0;
    };
    const LyricsRecord* best = nullptr;
    double bestScore = 0;
    for (const LyricsRecord& record : records) {
        if (record.ToLyrics().Empty() || mismatch(record) > kMaxLyricsDurationMismatch) continue;
        const double score = (record.syncedLyrics.empty() ? 0 : 1000) - mismatch(record);
        if (!best || score > bestScore) {
            best = &record;
            bestScore = score;
        }
    }
    return best ? std::optional<LyricsRecord>(*best) : std::nullopt;
}

namespace json {

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {}

    void SkipSpace() {
        while (position_ < text_.size() && std::string_view(" \t\r\n").find(text_[position_]) != std::string_view::npos) ++position_;
    }

    char Peek() {
        SkipSpace();
        return position_ < text_.size() ? text_[position_] : '\0';
    }

    bool Consume(char expected) {
        if (Peek() != expected) return false;
        ++position_;
        return true;
    }

    bool ReadString(std::string& out) {
        out.clear();
        if (!Consume('"')) return false;
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') return true;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) return false;
            const char escape = text_[position_++];
            switch (escape) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': if (!ReadUnicodeEscape(out)) return false; break;
                default: out.push_back(escape); break;
            }
        }
        return false;
    }

    bool ReadNumber(double& out) {
        SkipSpace();
        const size_t start = position_;
        while (position_ < text_.size() && std::string_view("+-.eE0123456789").find(text_[position_]) != std::string_view::npos) ++position_;
        if (position_ == start) return false;
        out = std::strtod(std::string(text_.substr(start, position_ - start)).c_str(), nullptr);
        return true;
    }

    bool ConsumeWord(std::string_view word) {
        SkipSpace();
        if (text_.substr(position_, word.size()) != word) return false;
        position_ += word.size();
        return true;
    }

    bool SkipValue(int depth = 0) {
        if (depth > 32) return false;
        std::string ignored;
        double number = 0;
        switch (Peek()) {
            case '"': return ReadString(ignored);
            case 't': return ConsumeWord("true");
            case 'f': return ConsumeWord("false");
            case 'n': return ConsumeWord("null");
            case '[':
                Consume('[');
                if (Consume(']')) return true;
                do {
                    if (!SkipValue(depth + 1)) return false;
                } while (Consume(','));
                return Consume(']');
            case '{':
                Consume('{');
                if (Consume('}')) return true;
                do {
                    if (!ReadString(ignored) || !Consume(':') || !SkipValue(depth + 1)) return false;
                } while (Consume(','));
                return Consume('}');
            default: return ReadNumber(number);
        }
    }

private:
    bool ReadHex4(uint32_t& out) {
        if (position_ + 4 > text_.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            uint32_t digit;
            if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
            else return false;
            out = out * 16 + digit;
        }
        return true;
    }

    bool ReadUnicodeEscape(std::string& out) {
        uint32_t code = 0;
        if (!ReadHex4(code)) return false;
        if (code >= 0xD800 && code < 0xDC00 && text_.substr(position_, 2) == "\\u") {
            position_ += 2;
            uint32_t low = 0;
            if (!ReadHex4(low)) return false;
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | code >> 6));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | code >> 12));
            out.push_back(static_cast<char>(0x80 | (code >> 6 & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | code >> 18));
            out.push_back(static_cast<char>(0x80 | (code >> 12 & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code >> 6 & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        return true;
    }

    std::string_view text_;
    size_t position_ = 0;
};

inline bool ReadRecord(Reader& reader, LyricsRecord& record) {
    if (!reader.Consume('{')) return false;
    if (reader.Consume('}')) return true;
    std::string key;
    do {
        if (!reader.ReadString(key) || !reader.Consume(':')) return false;
        const char next = reader.Peek();
        bool ok = true;
        if (key == "duration" && next != 'n') ok = reader.ReadNumber(record.duration);
        else if (key == "instrumental" && next == 't') ok = reader.ConsumeWord("true"), record.instrumental = true;
        else if (key == "plainLyrics" && next == '"') ok = reader.ReadString(record.plainLyrics);
        else if (key == "syncedLyrics" && next == '"') ok = reader.ReadString(record.syncedLyrics);
        else ok = reader.SkipValue();
        if (!ok) return false;
    } while (reader.Consume(','));
    return reader.Consume('}');
}

}  // namespace json

// Reads either one object, from /api/get, or an array of them, from /api/search. Whatever parsed
// before an error is kept.
inline std::vector<LyricsRecord> ParseLyricsRecords(std::string_view body) {
    std::vector<LyricsRecord> records;
    json::Reader reader(body);
    if (reader.Peek() == '{') {
        LyricsRecord record;
        if (json::ReadRecord(reader, record)) records.push_back(std::move(record));
        return records;
    }
    if (!reader.Consume('[') || reader.Consume(']')) return records;
    do {
        LyricsRecord record;
        if (!json::ReadRecord(reader, record)) break;
        records.push_back(std::move(record));
    } while (reader.Consume(','));
    return records;
}

inline std::string PercentEncode(std::string_view text) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
                           byte == '-' || byte == '_' || byte == '.' || byte == '~';
        if (plain) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 15]);
        }
    }
    return out;
}

// FNV-1a, for cache file names. Not security sensitive.
inline uint64_t LyricsCacheKey(std::string_view title, std::string_view artist) {
    uint64_t hash = 14695981039346656037ull;
    const auto add = [&hash](std::string_view text) {
        for (const char c : text) {
            hash ^= static_cast<unsigned char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            hash *= 1099511628211ull;
        }
    };
    add(artist);
    add("\n");
    add(title);
    return hash;
}

}  // namespace noitch
