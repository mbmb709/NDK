#include "Pixelizer.hpp"
#include "Schematic.hpp"
#include <cassert>
#include <cmath>
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
    // Avoid game engine "enter", shrink and fade transitions as the camera approaches.
    assert(result.gdObjectString.find(",64,1,67,1;")!=std::string::npos);
    size_t flags=0;
    for (size_t index=0; (index=result.gdObjectString.find(",64,1,67,1;",index))!=std::string::npos; index+=11) ++flags;
    assert(flags==static_cast<size_t>(result.objectCount));
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
    // New, higher quality settings keep original image detail instead of being capped at 128px.
    auto detail256=pixelizer::makePreview(large,256);
    auto detail384=pixelizer::makePreview(large,384);
    auto detail512=pixelizer::makePreview(large,512);
    assert(detail256.width==256 && detail256.height==171);
    assert(detail384.width==384 && detail384.height==256);
    assert(detail512.width==480 && detail512.height==320);
    pixelizer::Image photo;
    photo.width=1024; photo.height=512; photo.channels=3;
    photo.bytes.assign(static_cast<size_t>(photo.width)*photo.height*3,255);
    auto detail512big=pixelizer::makePreview(photo,512);
    assert(detail512big.width==512 && detail512big.height==256);
    assert(detail512big.bytes.size()==static_cast<size_t>(512*256*4));
    assert(pixelizer::kMaxSelectableDimension==512);

    // Increasing resolution creates more, smaller objects within an identical footprint.
    auto smallGrid=pixelizer::convert(photo,1000,700,32);
    auto largeGrid=pixelizer::convert(photo,1000,700,512);
    assert(smallGrid.columns==32 && smallGrid.rows==16 && smallGrid.objectCount==512);
    assert(largeGrid.columns==512 && largeGrid.rows==256 && largeGrid.objectCount==131072);
    assert(largeGrid.gdObjectString.find(",64,1,67,1;")!=std::string::npos);
    assert(pixelizer::pixelSizeForGrid(32,16)==15.0);
    assert(pixelizer::pixelSizeForGrid(512,256)==0.9375);
    assert(pixelizer::pixelSizeForGrid(32,16)*32==pixelizer::kCanvasMaxUnits);
    assert(pixelizer::pixelSizeForGrid(512,256)*512==pixelizer::kCanvasMaxUnits);
    assert(pixelizer::pixelSizeForGrid(32,16)*16==pixelizer::pixelSizeForGrid(512,256)*256);

    // Inspect serialized objects: the 32 key is the GD scale (not always 1 anymore).
    auto objectProperty=[](std::string const& encoded, std::string const& property) {
        const auto first=encoded.substr(0, encoded.find(';'));
        const auto needle=","+property+",";
        const auto pos=first.find(needle);
        assert(pos!=std::string::npos);
        auto valuePos=pos+needle.size();
        return std::stod(first.substr(valuePos, first.find(',', valuePos)-valuePos));
    };
    const double smallScale=objectProperty(smallGrid.gdObjectString,"32");
    const double largeScale=objectProperty(largeGrid.gdObjectString,"32");
    assert(std::abs(smallScale-2.0)<0.0001);
    assert(std::abs(largeScale-0.125)<0.0001);

    // First cell center + half a cell must meet the same left edge at both resolutions.
    const double smallFirstX=objectProperty(smallGrid.gdObjectString,"2");
    const double largeFirstX=objectProperty(largeGrid.gdObjectString,"2");
    assert(std::abs((smallFirstX-15.0/2.0)-760.0)<0.001);
    assert(std::abs((largeFirstX-0.9375/2.0)-760.0)<0.001);

    // Adaptive mode merges large flat areas but keeps high contrast edges crisp.
    pixelizer::Image uniform;
    uniform.width=128; uniform.height=128; uniform.channels=4;
    uniform.bytes.assign(128*128*4,255);
    auto flat=pixelizer::convertAdaptive(uniform,0,0,128);
    assert(flat.columns==128 && flat.rows==128);
    assert(flat.objectCount==64); // 8x8 blocks of 16x16, instead of 16,384 cells
    assert(flat.gdObjectString.find(",32,16.0000,64,1,67,1;")!=std::string::npos);
    // A sharp vertical boundary forces small blocks near the edge.
    for(int y=0;y<128;++y) for(int x=63;x<128;++x) {
        const auto i=(static_cast<size_t>(y)*128+x)*4;
        uniform.bytes[i]=0; uniform.bytes[i+1]=0; uniform.bytes[i+2]=0;
    }
    auto edged=pixelizer::convertAdaptive(uniform,0,0,128);
    assert(edged.objectCount>flat.objectCount);
    assert(edged.objectCount<128*128);
    // Adaptive and uniform occupy the same fixed-size image canvas.
    assert(edged.columns==128 && edged.rows==128);
    assert(edged.gdObjectString.find(",64,1,67,1;")!=std::string::npos);
    std::cout << "OK: 5 objects, adaptive pixelizer, .schem round-trip, malformed file rejected\n";
}
