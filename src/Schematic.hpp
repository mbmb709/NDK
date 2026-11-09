#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace schem {
constexpr std::string_view kMagic = "GDSCHEM1\n";
constexpr size_t kMaxPayload = 8 * 1024 * 1024;

struct Document {
    std::string kind; // "image" or "objects"
    std::string displayName;
    std::string gdObjectString;
};

// A custom Geometry Dash interchange format. NOT Minecraft's NBT/Sponge .schem!
std::string serialize(Document const& document);
std::optional<Document> parse(std::string_view bytes);
// Quick sanity validation before passing strings to Geometry Dash's object parser.
bool isObjectString(std::string_view gdString);
}
