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

constexpr int kMaxDimension = 48;  // Legacy default for core and tests
constexpr int kMaxSelectableDimension = 512; // Extreme mode: up to 262,144 objects; can overload GD
constexpr double kPixelUnits = 7.5; // GD pixel object 917 is 7.5 level units at scale 1
constexpr double kCanvasMaxUnits = 480.0; // Same on-screen footprint for all resolutions

// The longest image side in GD level units, independent of pixel resolution.
double pixelSizeForGrid(int columns, int rows, double imageSize = kCanvasMaxUnits);

Conversion convert(Image const& image, double centerX, double centerY,
                   int maxDimension = kMaxDimension, double imageSize = kCanvasMaxUnits);
// Greedy maximal-square tiling, no arbitrary 16x16 tile boundaries.
// tolerance controls allowed RGB difference within a single merged square.
Conversion convertAdaptive(Image const& image, double centerX, double centerY,
                           int maxDimension = kMaxDimension, int sensitivity = 20,
                           double imageSize = kCanvasMaxUnits);
// Builds the low-resolution pixel-art image shown in the in-editor preview.
Image makePreview(Image const& image, int maxDimension = kMaxDimension);
}
