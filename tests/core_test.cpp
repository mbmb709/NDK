#include "Pixelizer.hpp"
#include "Schematic.hpp"
#include <cassert>
#include <iostream>

int main() {
    pixelizer::Image img{3, 2, 4, {
        255,0,0,255, 0,255,0,255, 0,0,255,255,
        0,0,0,0,  255,255,255,255, 0,0,0,255
    }};
    auto result=pixelizer::convert(img,100,150);
    assert(result.columns==3 && result.rows==2);
    assert(result.objectCount==5);
    assert(result.gdObjectString.find("1,917,2,")!=std::string::npos);
    schem::Document doc{"image","test.png",result.gdObjectString};
    const auto encoded=schem::serialize(doc);
    assert(!encoded.empty());
    const auto decoded=schem::parse(encoded);
    assert(decoded && decoded->displayName=="test.png");
    assert(decoded->gdObjectString==result.gdObjectString);
    assert(!schem::parse("bad-file"));
    auto bad = encoded;
    bad.pop_back();
    assert(!schem::parse(bad)); // truncated file
    assert(schem::serialize({"unknown", "test", result.gdObjectString}).empty());
    pixelizer::Image large;
    large.width=480;
    large.height=320;
    large.channels=3;
    large.bytes.assign(480*320*3,255);
    auto scaled=pixelizer::convert(large,0,0);
    assert(scaled.columns<=48 && scaled.rows<=48 && scaled.objectCount<=48*48);
    // User-controlled resolution: preserve aspect ratio, and both decrease and increase detail.
    auto low=pixelizer::convert(large,0,0,16);
    auto medium=pixelizer::convert(large,0,0,64);
    auto high=pixelizer::convert(large,0,0,128);
    assert(low.columns==16 && low.rows==11 && low.objectCount==16*11);
    assert(medium.columns==64 && medium.rows==43 && medium.objectCount==64*43);
    assert(high.columns==128 && high.rows==85 && high.objectCount==128*85);
    assert(low.objectCount < medium.objectCount && medium.objectCount < high.objectCount);
    auto lowPreview=pixelizer::makePreview(large,16);
    auto highPreview=pixelizer::makePreview(large,128);
    assert(lowPreview.width==low.columns && lowPreview.height==low.rows);
    assert(highPreview.width==high.columns && highPreview.height==high.rows);
    assert(lowPreview.channels==4 && highPreview.bytes.size()==static_cast<size_t>(high.columns*high.rows*4));
    assert(lowPreview.bytes[0]==255 && lowPreview.bytes[1]==255 && lowPreview.bytes[2]==255 && lowPreview.bytes[3]==255);

    std::cout << "OK: 5 objects, .schem round-trip, malformed file rejected\n";
}
