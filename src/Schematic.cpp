#include "Schematic.hpp"
#include <charconv>
#include <cctype>

namespace schem {
namespace {
    bool readLine(std::string_view data, size_t& cursor, std::string_view& line) {
        const auto end = data.find('\n', cursor);
        if (end == std::string_view::npos) return false;
        line = data.substr(cursor, end - cursor);
        cursor = end + 1;
        return true;
    }
    bool readLength(std::string_view data, size_t& cursor, size_t& value) {
        std::string_view line;
        if (!readLine(data, cursor, line) || line.empty()) return false;
        const auto [end, err] = std::from_chars(line.data(), line.data() + line.size(), value);
        return err == std::errc{} && end == line.data() + line.size();
    }
}

bool isObjectString(std::string_view value) {
    if (value.empty() || value.size() > kMaxPayload || value.back() != ';') return false;
    // The game uses numeric object properties plus a few textual property values.
    // Block control characters, markup and any nested file-like data.
    for (unsigned char c : value) {
        if (c < 32 || c == 127) return false;
    }
    return value.find("1,") != std::string_view::npos && value.find("2,") != std::string_view::npos && value.find("3,") != std::string_view::npos;
}

std::string serialize(Document const& d) {
    if ((d.kind != "image" && d.kind != "objects") || d.displayName.size() > 512 || !isObjectString(d.gdObjectString))
        return {};
    std::string out;
    out.reserve(d.gdObjectString.size() + d.displayName.size() + 80);
    out += kMagic;
    out += d.kind + "\n";
    out += std::to_string(d.displayName.size()) + "\n";
    out += d.displayName;
    out += std::to_string(d.gdObjectString.size()) + "\n";
    out += d.gdObjectString;
    return out;
}

std::optional<Document> parse(std::string_view bytes) {
    if (bytes.size() > kMaxPayload + 2048 || !bytes.starts_with(kMagic)) return std::nullopt;
    size_t pos = kMagic.size();
    std::string_view kind;
    if (!readLine(bytes, pos, kind) || (kind != "image" && kind != "objects")) return std::nullopt;
    size_t nameLength = 0;
    if (!readLength(bytes, pos, nameLength) || nameLength > 512 || nameLength > bytes.size() - pos) return std::nullopt;
    std::string name(bytes.substr(pos, nameLength));
    pos += nameLength;
    size_t payloadSize = 0;
    if (!readLength(bytes, pos, payloadSize) || payloadSize > kMaxPayload || payloadSize != bytes.size() - pos) return std::nullopt;
    const auto payload = bytes.substr(pos, payloadSize);
    if (!isObjectString(payload)) return std::nullopt;
    return Document{std::string(kind), std::move(name), std::string(payload)};
}
}
