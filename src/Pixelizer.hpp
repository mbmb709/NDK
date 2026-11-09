#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pixelizer {
struct Image {
    int width = 0;
    int height = 0;
    int channels = 0; // 3 (RGB) or 4 (RGBA), row-major top-to-bottom
    std::vector<uint8_t> bytes;
};
struct Conversion {
    std::string gdObjectString;
    int columns = 0;
    int rows = 0;
    int objectCount = 0;
};

constexpr int kMaxDimension = 48;      // <= 2304 objects, safe initial cap
constexpr double kPixelUnits = 7.5;   // GD pixel object 917 is 7.5 level units at scale 1

Conversion convert(Image const& image, double centerX, double centerY);
}
