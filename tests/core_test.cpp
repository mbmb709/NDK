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

    std::cout << "OK: 5 objects, .schem round-trip, malformed file rejected\n";
}
