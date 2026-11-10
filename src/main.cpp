// Image & Schematic Editor - Geode v5.11.2 proof-of-concept
// Geometry Dash 2.2081, Android & Windows. Requires compiling/test with real SDK.
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/permission.hpp>

#include "Pixelizer.hpp"
#include "Schematic.hpp"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

using namespace geode::prelude;
namespace pick = geode::utils::file;

namespace {
void message(std::string const& title, std::string const& content) {
    FLAlertLayer::create(title.c_str(), content, "OK")->show();
}

std::string filename(std::filesystem::path const& path) {
    return path.filename().string();
}

std::string cleanFilename(std::string str) {
    if (str.empty()) return "schematic";
    for (char& c : str) {
        if (c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') c='_';
    }
    if (str.size()>80) str.resize(80);
    auto pos=str.find_last_of('.');
    if (pos!=std::string::npos) str.resize(pos);
    return str.empty() ? "schematic" : str;
}

std::filesystem::path suggestedDownload(std::string const& name) {
#ifdef GEODE_IS_ANDROID
    // Android's OS SaveFile picker mediates scoped storage access.
    return std::filesystem::path{"/storage/emulated/0/Download/" + cleanFilename(name) + ".schem"};
#elif defined(GEODE_IS_WINDOWS)
    if (auto home=std::getenv("USERPROFILE")) {
        return std::filesystem::path{std::string(home) + "/Downloads/" + cleanFilename(name) + ".schem"};
    }
    return Mod::get()->getSaveDir() / (cleanFilename(name) + ".schem");
#else
    return Mod::get()->getSaveDir() / (cleanFilename(name) + ".schem");
#endif
}

// Show an actionable diagnostic rather than a generic PNG/JPG error.
std::string s_imageError;
int s_pixelResolution = 256;
bool s_adaptiveMode = true;
constexpr int kPixelSteps[] = {16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512};

// Read the selected image only once. The compressed image may be large, but
// the editor should only keep a small preview to avoid OOM and GPU texture limits.
bool decodePickedImage(std::filesystem::path const& path, CCImage& image) {
    auto file = pick::readBinary(path);
    if (file.isErr()) {
        s_imageError = "Cannot read selected file: " + file.unwrapErr() + ". Try a local copy in Downloads or grant Geode storage access.";
        return false;
    }
    auto& bytes = file.unwrap();
    if (bytes.empty()) {
        s_imageError = "Selected image file is empty.";
        return false;
    }
    // CCImage's decoder takes an int byte count, not size_t.
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        s_imageError = "File is too large for this image decoder (over 2 GB).";
        return false;
    }
    if (!image.initWithImageData(bytes.data(), static_cast<int>(bytes.size()), CCImage::kFmtUnKnown)) {
        s_imageError = "Image decoder failed. Try a standard RGB PNG or JPG.";
        return false;
    }
    return true;
}

constexpr int kPreviewMaxEdge = 512;

// Keep aspect ratio at any source resolution. Sample each destination pixel from
// a small, regularly-spaced grid in its source area to bound CPU work and RAM.
pixelizer::Image loadImage(std::filesystem::path const& path) {
    s_imageError.clear();
    pixelizer::Image result;
    CCImage image;
    if (!decodePickedImage(path, image)) return result;
    const int sourceW = image.getWidth();
    const int sourceH = image.getHeight();
    const auto* source = image.getData();
    const int channels = image.hasAlpha() ? 4 : 3;
    if (sourceW <= 0 || sourceH <= 0 || !source) {
        s_imageError = "Image decoder returned no pixels or invalid dimensions.";
        return result;
    }
    if (image.getBitsPerComponent() != 8) {
        s_imageError = "Unsupported color depth. Convert to 8-bit RGB PNG/JPG.";
        return result;
    }

    const double factor = std::min({1.0,
        static_cast<double>(kPreviewMaxEdge) / sourceW,
        static_cast<double>(kPreviewMaxEdge) / sourceH});
    const int targetW = std::clamp(
        static_cast<int>(std::lround(sourceW * factor)), 1, kPreviewMaxEdge);
    const int targetH = std::clamp(
        static_cast<int>(std::lround(sourceH * factor)), 1, kPreviewMaxEdge);

    result.width = targetW;
    result.height = targetH;
    result.channels = 4;
    result.bytes.resize(static_cast<size_t>(targetW) * targetH * 4);

    for (int y = 0; y < targetH; ++y) {
        const int y0 = static_cast<int>(static_cast<int64_t>(y) * sourceH / targetH);
        const int y1 = static_cast<int>(static_cast<int64_t>(y + 1) * sourceH / targetH);
        const int samplesY = std::min(4, y1 - y0);
        for (int x = 0; x < targetW; ++x) {
            const int x0 = static_cast<int>(static_cast<int64_t>(x) * sourceW / targetW);
            const int x1 = static_cast<int>(static_cast<int64_t>(x + 1) * sourceW / targetW);
            const int samplesX = std::min(4, x1 - x0);
            uint64_t r = 0, g = 0, b = 0, alpha = 0;
            for (int sy = 0; sy < samplesY; ++sy) {
                const int srcY = y0 + (2 * sy + 1) * (y1 - y0) / (2 * samplesY);
                for (int sx = 0; sx < samplesX; ++sx) {
                    const int srcX = x0 + (2 * sx + 1) * (x1 - x0) / (2 * samplesX);
                    const size_t src = (static_cast<size_t>(srcY) * sourceW + srcX) * channels;
                    const uint32_t a = channels == 4 ? source[src + 3] : 255;
                    alpha += a;
                    r += static_cast<uint64_t>(source[src]) * a;
                    g += static_cast<uint64_t>(source[src + 1]) * a;
                    b += static_cast<uint64_t>(source[src + 2]) * a;
                }
            }
            const size_t dst = (static_cast<size_t>(y) * targetW + x) * 4;
            const int sampleCount = samplesX * samplesY;
            result.bytes[dst] = alpha ? static_cast<uint8_t>(r / alpha) : 0;
            result.bytes[dst + 1] = alpha ? static_cast<uint8_t>(g / alpha) : 0;
            result.bytes[dst + 2] = alpha ? static_cast<uint8_t>(b / alpha) : 0;
            result.bytes[dst + 3] = static_cast<uint8_t>(alpha / sampleCount);
        }
    }
    return result;
}

CCSprite* spriteFromImage(pixelizer::Image const& image) {
    if (image.width <= 0 || image.height <= 0 || image.channels != 4 || image.bytes.empty()) {
        s_imageError = "Preview has no pixel data.";
        return nullptr;
    }
    auto* texture = new CCTexture2D();
    if (!texture->initWithData(
            image.bytes.data(), kCCTexture2DPixelFormat_RGBA8888,
            static_cast<unsigned int>(image.width), static_cast<unsigned int>(image.height),
            CCSize(static_cast<float>(image.width), static_cast<float>(image.height)))) {
        s_imageError = "Could not create the image preview texture.";
        texture->release();
        return nullptr;
    }
    // Nearest-neighbor preview: make individual pixels crisp, not blurry.
    texture->setAliasTexParameters();
    auto* sprite = CCSprite::createWithTexture(texture);
    texture->release();
    return sprite;
}

// Dragging is handled separately from GD objects; visual previews are not saved.
class PreviewDragLayer final : public CCLayer {
    CCSprite* m_sprite=nullptr;     // owned by LevelEditorLayer::m_objectLayer
    CCNode* m_objectLayer=nullptr;  // owned by the editor
    CCPoint m_offset={0,0};
public:
    static PreviewDragLayer* create(CCSprite* sprite, CCNode* objectLayer) {
        auto* self=new PreviewDragLayer;
        if (self->CCLayer::init()) {
            self->m_sprite=sprite;
            self->m_objectLayer=objectLayer;
            self->setTouchEnabled(true);
            self->autorelease();
            return self;
        }
        delete self;
        return nullptr;
    }
    void registerWithTouchDispatcher() override {
        // Above regular editor touches, below modal popups.
        CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this,-300,true);
    }
    bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
        if (!m_sprite || !m_objectLayer || !m_sprite->getParent()) return false;
        auto p=m_objectLayer->convertToNodeSpace(touch->getLocation());
        if (!m_sprite->boundingBox().containsPoint(p)) return false;
        m_offset=m_sprite->getPosition()-p;
        return true;
    }
    void ccTouchMoved(CCTouch* touch, CCEvent*) override {
        if (m_sprite && m_sprite->getParent()) {
            m_sprite->setPosition(m_objectLayer->convertToNodeSpace(touch->getLocation())+m_offset);
        }
    }
};
} // namespace

class ImageSchematicPopup;

class $modify(ImageSchematicEditorUI, EditorUI) {
public:
    struct Fields {
        std::filesystem::path imagePath;
        std::string imageName;
        CCSprite* ghost=nullptr;
        PreviewDragLayer* drag=nullptr;
    };

    bool init(LevelEditorLayer* level) {
        if (!EditorUI::init(level)) return false;
        auto menu=CCMenu::create();
        auto win=CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({win.width-31.f, win.height-95.f});
        auto* button=CCMenuItemSpriteExtra::create(ButtonSprite::create("IMG"), this, menu_selector(ImageSchematicEditorUI::onOpenImageMenu));
        button->setScale(.58f);
        menu->addChild(button);
        menu->setID("schematic.image-editor/menu");
        this->addChild(menu,999);
        return true;
    }

    void onOpenImageMenu(CCObject*);

    void clearGhost() {
        if (m_fields->drag) {
            m_fields->drag->removeFromParentAndCleanup(true);
            m_fields->drag=nullptr;
        }
        if (m_fields->ghost) {
            m_fields->ghost->removeFromParentAndCleanup(true);
            m_fields->ghost=nullptr;
        }
    }

    void selectImage(std::filesystem::path const& path) {
        clearGhost();
        m_fields->imagePath=path;
        m_fields->imageName=filename(path);
    }

    std::string chosenImageName() { return m_fields->imageName; }
    bool hasImage() { return !m_fields->imageName.empty(); }

    CCPoint placementCenter() {
        if (m_fields->ghost) return m_fields->ghost->getPosition();
        const auto win=CCDirector::sharedDirector()->getWinSize();
        return m_editorLayer->m_objectLayer->convertToNodeSpace({win.width/2.f,win.height/2.f});
    }

    bool showVisualImage() {
        if (!hasImage()) return false;
        const auto center=placementCenter();
        clearGhost();
        auto rgba=loadImage(m_fields->imagePath);
        if (rgba.width==0) return false;
        auto sampled=pixelizer::makePreview(rgba,s_pixelResolution);
        if (sampled.width==0 || sampled.height==0) {
            s_imageError = "Image cannot be sampled at this pixel resolution.";
            return false;
        }
        auto* sprite=spriteFromImage(sampled);
        if (!sprite) return false;
        sprite->setPosition(center);
        sprite->setOpacity(180);
        const auto pixelUnits=pixelizer::pixelSizeForGrid(sampled.width,sampled.height);
        sprite->setScaleX(float(sampled.width*pixelUnits)/sprite->getContentSize().width);
        sprite->setScaleY(float(sampled.height*pixelUnits)/sprite->getContentSize().height);
        sprite->setID("schematic.image-editor/visual-preview");
        m_editorLayer->m_objectLayer->addChild(sprite,5000);
        m_fields->ghost=sprite;
        auto* drag=PreviewDragLayer::create(sprite,m_editorLayer->m_objectLayer);
        if (drag) {
            drag->setID("schematic.image-editor/drag-handler");
            this->addChild(drag,998);
            m_fields->drag=drag;
        }
        return true;
    }

    std::optional<pixelizer::Conversion> buildPixelObjects() {
        if (!hasImage()) return std::nullopt;
        auto rgba=loadImage(m_fields->imagePath);
        if (!rgba.width) return std::nullopt;
        const auto center=placementCenter();
        auto result=s_adaptiveMode ? pixelizer::convertAdaptive(rgba,center.x,center.y,s_pixelResolution) : pixelizer::convert(rgba,center.x,center.y,s_pixelResolution);
        if (result.objectCount==0) return std::nullopt;
        return result;
    }

    bool importPixels(pixelizer::Conversion const& pixels) {
        if (pixels.gdObjectString.empty()) return false;
        // Native GD paste registers objects with the level editor. Source gets checked by the codec.
        const auto pasted=this->pasteObjects(pixels.gdObjectString,true,false);
        if (!pasted || pasted->count()==0) return false;
        clearGhost();
        this->updateButtons();
        return true;
    }

    std::optional<std::string> copySelectedAsGDString() {
        auto* objects=CCArray::create();
        if (m_selectedObject) objects->addObject(m_selectedObject);
        else if (auto* selected=this->getSelectedObjects()) objects->addObjectsFromArray(selected);
        if (objects->count()==0) return std::nullopt;
        auto* gm=GameManager::sharedState();
        auto previous=gm->m_editorClipboard;
        this->copyObjects(objects, true, true);
        auto copied=gm->m_editorClipboard;
        gm->m_editorClipboard=previous; // do not erase the player's actual clipboard
        if (!schem::isObjectString(copied)) return std::nullopt;
        return copied;
    }
};

class ImageSchematicPopup final : public geode::Popup {
    CCLabelBMFont* m_name=nullptr;
    CCLabelBMFont* m_detail=nullptr;
    CCLabelBMFont* m_count=nullptr;
    CCLabelBMFont* m_mode=nullptr;

    void addButton(CCMenu* menu, char const* text, SEL_MenuHandler cb, float x, float y, float width=1.f) {
        auto* item=CCMenuItemSpriteExtra::create(ButtonSprite::create(text), this, cb);
        item->setPosition({x,y});
        item->setScale(width);
        menu->addChild(item);
    }

    bool init() {
        if (!Popup::init(390.f,265.f)) return false;
        this->setTitle("Image & Schematics");
        auto* menu=CCMenu::create();
        menu->setPosition({195,130});
        m_mainLayer->addChild(menu);

        // Four evenly spaced rows; buttons do not overlap on small screens.
        addButton(menu,"Choose Image",menu_selector(ImageSchematicPopup::onChooseImage),-95,77,.66f);
        addButton(menu,"Open .schem",menu_selector(ImageSchematicPopup::onOpenSchematic),95,77,.66f);

        addButton(menu,"-",menu_selector(ImageSchematicPopup::onFewerPixels),-125,14,.68f);
        addButton(menu,"+",menu_selector(ImageSchematicPopup::onMorePixels),125,14,.68f);
        addButton(menu,"Mode",menu_selector(ImageSchematicPopup::onToggleAdaptive),0,14,.62f);

        addButton(menu,"Preview",menu_selector(ImageSchematicPopup::onPreview),-97,-36,.73f);
        addButton(menu,"Import Pixel Art",menu_selector(ImageSchematicPopup::onImportPixels),97,-36,.66f);
        addButton(menu,"Export .schem",menu_selector(ImageSchematicPopup::onDownloadImageSchematic),-97,-84,.62f);
        addButton(menu,"Save Selection",menu_selector(ImageSchematicPopup::onSaveSelection),97,-84,.62f);

        m_name=CCLabelBMFont::create("No image selected", "bigFont.fnt");
        m_name->setPosition({195,172});
        m_name->setScale(.44f);
        m_mainLayer->addChild(m_name);

        s_pixelResolution=std::clamp(Mod::get()->getSavedValue<int>("pixel-resolution-v2",256),16,512);
        s_adaptiveMode=Mod::get()->getSavedValue<bool>("adaptive-mode",true);
        m_detail=CCLabelBMFont::create("", "bigFont.fnt");
        m_detail->setPosition({195,145});
        m_detail->setScale(.51f);
        m_mainLayer->addChild(m_detail);
        m_count=CCLabelBMFont::create("", "bigFont.fnt");
        m_count->setPosition({195,121});
        m_count->setScale(.38f);
        m_mainLayer->addChild(m_count);
        m_mode=CCLabelBMFont::create("", "bigFont.fnt");
        m_mode->setPosition({195,101});
        m_mode->setScale(.34f);
        m_mainLayer->addChild(m_mode);
        refreshPixelInfo();

        if (auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get())) {
            if (ui->hasImage()) setFilename(ui->chosenImageName());
        }
        return true;
    }

    void refreshPixelInfo() {
        if (m_detail) {
            const auto label="Pixel detail: "+std::to_string(s_pixelResolution)+" px (fixed size)";
            m_detail->setString(label.c_str());
        }
        if (m_count) {
            const auto maximum=s_pixelResolution*s_pixelResolution;
            const auto label="Up to "+std::to_string(maximum)+" objects"+(s_pixelResolution>=256 ? " (! Heavy!)" : s_pixelResolution>=128 ? " (slow)" : "");
            m_count->setString(label.c_str());
        }
        if (m_mode) m_mode->setString(s_adaptiveMode ? "Adaptive ON: big flat blocks, small edges" : "Uniform: one object per pixel");
    }

    void setPixelResolution(int next) {
        s_pixelResolution=std::clamp(next,16,pixelizer::kMaxSelectableDimension);
        Mod::get()->setSavedValue("pixel-resolution-v2",s_pixelResolution);
        refreshPixelInfo();
    }

    void onToggleAdaptive(CCObject*) {
        s_adaptiveMode=!s_adaptiveMode;
        Mod::get()->setSavedValue("adaptive-mode",s_adaptiveMode);
        refreshPixelInfo();
    }

    void onFewerPixels(CCObject*) {
        for (int i=10;i>0;--i) if (s_pixelResolution>=kPixelSteps[i]) {
            setPixelResolution(kPixelSteps[i-1]);
            return;
        }
        setPixelResolution(kPixelSteps[0]);
    }

    void onMorePixels(CCObject*) {
        for (const auto step:kPixelSteps) if (step>s_pixelResolution) {
            setPixelResolution(step);
            return;
        }
        setPixelResolution(kPixelSteps[10]);
    }

    void setFilename(std::string const& name) {
        // Preserve the full filename in state; only UI text is trimmed.
        auto display=name;
        if (display.size()>40) display=display.substr(0,18)+"..."+display.substr(display.size()-17);
        m_name->setString(display.c_str());
    }

    void saveDocument(schem::Document doc) {
        const auto bytes=schem::serialize(doc);
        if (bytes.empty()) {
            message("Schematic", "Nothing to export, or schematic too large.");
            return;
        }
        pick::FilePickOptions opts;
        opts.defaultPath=suggestedDownload(doc.displayName);
        opts.filters.push_back({"Geometry Dash schematics", {"*.schem"}});
        geode::async::spawn(pick::pick(pick::PickMode::SaveFile,std::move(opts)),[bytes](auto result) {
            if (result.isErr()) {
                message("Schematic", "The file save dialog failed.");
                return;
            }
            const auto path=result.unwrap();
            if (!path) return; // user cancelled
            auto write=pick::writeString(*path,bytes);
            if (write.isErr()) message("Schematic", "Could not save to the selected folder. Choose Downloads.");
            else message("Schematic saved", "The .schem file was saved to the location you chose.");
        });
    }

    void openImagePicker() {
        pick::FilePickOptions opts;
        opts.filters.push_back({"Images", {"*.png","*.jpg","*.jpeg","*.webp"}});
        Ref<ImageSchematicPopup> keep(this);
        geode::async::spawn(pick::pick(pick::PickMode::OpenFile, std::move(opts)), [keep](auto result) {
            if (result.isErr()) {
                message("Image", "Image picker failed. Check Geode Launcher storage permissions.");
                return;
            }
            const auto path = result.unwrap();
            if (!path) return;
            if (auto* ui = static_cast<ImageSchematicEditorUI*>(EditorUI::get())) {
                ui->selectImage(*path);
                keep->setFilename(ui->chosenImageName());
            }
        });
    }

    void onChooseImage(CCObject*) {
#ifdef GEODE_IS_ANDROID
        using geode::utils::permission::Permission;
        if (!geode::utils::permission::getPermissionStatus(Permission::ReadAllFiles)) {
            Ref<ImageSchematicPopup> keep(this);
            geode::utils::permission::requestPermission(Permission::ReadAllFiles, [keep](bool granted) {
                if (granted) {
                    keep->openImagePicker();
                } else {
                    message("Storage permission", "Geode needs access to the selected image. You can grant storage access in Android settings, or use a local file.");
                }
            });
            return;
        }
#endif
        openImagePicker();
    }

    void onPreview(CCObject*) {
        auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get());
        if (!ui || !ui->hasImage()) {
            message("Image", "Choose an image first.");
            return;
        }
        if (!ui->showVisualImage()) {
            message("Image", s_imageError.empty() ? "Cannot create a preview. Try a smaller JPG." : s_imageError);
            return;
        }
        onClose(nullptr);
    }

    void onImportPixels(CCObject*) {
        auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get());
        if (!ui || !ui->hasImage()) {message("Image", "Choose an image first.");return;}
        auto pixels=ui->buildPixelObjects();
        if (!pixels) {message("Image", s_imageError.empty() ? "No usable pixels in image." : s_imageError);return;}
        if (!ui->importPixels(*pixels)) {message("Image", "Game editor failed to create objects.");return;}
        onClose(nullptr);
    }

    void onDownloadImageSchematic(CCObject*) {
        auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get());
        if (!ui || !ui->hasImage()) {message("Schematic", "Choose an image first.");return;}
        auto pixels=ui->buildPixelObjects();
        if (!pixels) {message("Schematic", "This image could not be converted.");return;}
        saveDocument({"image",ui->chosenImageName(),std::move(pixels->gdObjectString)});
    }

    void onSaveSelection(CCObject*) {
        auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get());
        if (!ui) return;
        auto raw=ui->copySelectedAsGDString();
        if (!raw) {message("Schematic", "Select one or more level objects first.");return;}
        saveDocument({"objects","selected-objects",std::move(*raw)});
    }

    void onOpenSchematic(CCObject*) {
        pick::FilePickOptions opts;
        opts.defaultPath=suggestedDownload("schematic");
        opts.filters.push_back({"Geometry Dash schematics", {"*.schem"}});
        Ref<ImageSchematicPopup> keep(this);
        geode::async::spawn(pick::pick(pick::PickMode::OpenFile,std::move(opts)),[keep](auto result) {
            if (result.isErr()) {message("Schematic", "Could not open file picker.");return;}
            const auto path=result.unwrap();
            if (!path) return;
            auto text=pick::readString(*path);
            if (text.isErr()) {message("Schematic", "Cannot read this file.");return;}
            auto document=schem::parse(text.unwrap());
            if (!document) {
                message("Schematic", "Invalid file. Only GDSCHEM1 .schem created by this mod is supported.");
                return;
            }
            auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get());
            if (!ui) {message("Schematic", "Open a level editor first.");return;}
            auto* pasted=ui->pasteObjects(document->gdObjectString,true,false);
            if (!pasted || pasted->count()==0) {message("Schematic", "Game editor rejected the schematic objects.");return;}
            ui->updateButtons();
            keep->onClose(nullptr);
        });
    }

public:
    static ImageSchematicPopup* create() {
        auto* popup=new ImageSchematicPopup;
        if (popup->init()) { popup->autorelease();return popup; }
        delete popup;
        return nullptr;
    }
};

void ImageSchematicEditorUI::onOpenImageMenu(CCObject*) {
    if (auto* popup=ImageSchematicPopup::create()) popup->show();
}
