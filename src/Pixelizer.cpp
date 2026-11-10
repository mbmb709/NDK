#include "Pixelizer.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace pixelizer {
namespace {
    struct RGB { int r=0, g=0, b=0, a=0; };

    RGB sample(Image const& img, int x, int y, int columns, int rows) {
        // Area average sampling. This keeps the full image within the object budget.
        const int x0 = x * img.width / columns;
        const int x1 = std::max(x0 + 1, (x + 1) * img.width / columns);
        const int y0 = y * img.height / rows;
        const int y1 = std::max(y0 + 1, (y + 1) * img.height / rows);
        uint64_t red=0, green=0, blue=0, alpha=0;
        for (int yy=y0; yy<y1; ++yy) for (int xx=x0; xx<x1; ++xx) {
            size_t i = (static_cast<size_t>(yy) * img.width + xx) * img.channels;
            int a = img.channels == 4 ? img.bytes[i+3] : 255;
            red += img.bytes[i] * a;
            green += img.bytes[i+1] * a;
            blue += img.bytes[i+2] * a;
            alpha += a;
        }
        const auto count = static_cast<uint64_t>(x1-x0) * (y1-y0);
        if (alpha == 0) return {};
        return {int(red/alpha), int(green/alpha), int(blue/alpha), int(alpha/count)};
    }

    // Preserve original aspect ratio while limiting maximum GD object count.
    std::pair<int, int> targetSize(Image const& img, int maxDimension) {
        const int limit = std::clamp(maxDimension, 8, kMaxSelectableDimension);
        const double factor=std::min({1., double(limit)/img.width, double(limit)/img.height});
        return {
            std::max(1,static_cast<int>(std::round(img.width * factor))),
            std::max(1,static_cast<int>(std::round(img.height * factor)))
        };
    }

    struct HSV { int h=0; double s=0., v=0.; };
    HSV toHSV(RGB p) {
        const double r=p.r/255., g=p.g/255., b=p.b/255.;
        const double mx=std::max({r,g,b}), mn=std::min({r,g,b}), d=mx-mn;
        double h=0;
        if(d>0) {
            if(mx==r) h=60*std::fmod((g-b)/d,6.);
            else if(mx==g) h=60*((b-r)/d+2.);
            else h=60*((r-g)/d+4.);
        }
        if(h<0) h+=360;
        // Geometry Dash represents hue rotation as [-180,180]. Zero is special in some versions.
        int hue=(static_cast<int>(std::lround(h))+180)%360-180;
        if (hue==0 && d>0) hue=1;
        return {hue, mx==0?0:d/mx, mx};
    }
}

Conversion convert(Image const& img, double cx, double cy, int maxDimension) {
    Conversion res;
    if (img.width <= 0 || img.height <= 0 || (img.channels != 3 && img.channels != 4)) return res;
    const auto n = static_cast<size_t>(img.width) * img.height;
    if (n>16'000'000 || img.bytes.size()!=n*img.channels) return res;
    const auto [columns, rows] = targetSize(img, maxDimension);
    res.columns = columns;
    res.rows = rows;
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out << std::setprecision(4);
    for (int y=0; y<res.rows; ++y) for(int x=0; x<res.columns; ++x) {
        const auto rgb=sample(img,x,y,res.columns,res.rows);
        if (rgb.a<64) continue;
        const auto hsv=toHSV(rgb);
        const double objX=cx+(x+.5-res.columns/2.)*kPixelUnits;
        const double objY=cy+(res.rows/2.-y-.5)*kPixelUnits;
        // 917: decorative pixel. 21: base channel (1010, white), 41/43: absolute HSV.
        out << "1,917,2," << objX << ",3," << objY
            << ",21,1010,41,1,43," << hsv.h << 'a' << hsv.s << 'a' << hsv.v << "a1a1"
            << ",24,5,25,-7,32,1;";
        ++res.objectCount;
    }
    res.gdObjectString=out.str();
    return res;
}
Image makePreview(Image const& img, int maxDimension) {
    Image preview;
    if (img.width <= 0 || img.height <= 0 || (img.channels != 3 && img.channels != 4)) return preview;
    const auto n=static_cast<size_t>(img.width)*img.height;
    if (n>16'000'000 || img.bytes.size()!=n*img.channels) return preview;
    const auto [columns, rows] = targetSize(img, maxDimension);
    preview.width = columns;
    preview.height = rows;
    preview.channels = 4;
    preview.bytes.reserve(static_cast<size_t>(columns)*rows*4);
    for (int y=0; y<rows; ++y) for (int x=0; x<columns; ++x) {
        const auto p = sample(img,x,y,columns,rows);
        preview.bytes.push_back(static_cast<uint8_t>(p.r));
        preview.bytes.push_back(static_cast<uint8_t>(p.g));
        preview.bytes.push_back(static_cast<uint8_t>(p.b));
        preview.bytes.push_back(static_cast<uint8_t>(p.a));
    }
    return preview;
}

}
