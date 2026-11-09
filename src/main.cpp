// Image & Schematic Editor - Geode v5.11.2 proof-of-concept
// Geometry Dash 2.2081, Android & Windows. Requires compiling/test with real SDK.
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/file.hpp>

#include "Pixelizer.hpp"
#include "Schematic.hpp"

#include <algorithm>
#include <cstdlib>
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

// Use Geode's file reader for paths supplied by the Android document picker.
// Cocos's FileUtils path resolution does not reliably open these files.
bool decodePickedImage(std::filesystem::path const& path, CCImage& image) {
    auto result = pick::readBinary(path);
    if (result.isErr()) {
        s_imageError = "Cannot read this file. Copy it to Downloads and select that copy.";
        return false;
    }
    auto& data = result.unwrap();
    if (data.empty()) {
        s_imageError = "Selected file is empty.";
        return false;
    }
    if (data.size() > 32'000'000) {
        s_imageError = "Image file is larger than 32 MB. Convert it to a smaller JPG.";
        return false;
    }
    if (!image.initWithImageData(data.data(), static_cast<int>(data.size()), CCImage::kFmtUnKnown)) {
        s_imageError = "Image format could not be decoded. Export as standard 8-bit RGB JPG.";
        return false;
    }
    return true;
}

CCSprite* spriteFromPickedImage(std::filesystem::path const& path) {
    CCImage image;
    if (!decodePickedImage(path, image)) return nullptr;
    auto* texture = new CCTexture2D();
    if (!texture->initWithImage(&image)) {
        s_imageError = "Preview texture could not be created. Try a JPG smaller than 1920 px.";
        texture->release();
        return nullptr;
    }
    auto* sprite = CCSprite::createWithTexture(texture);
    texture->release();
    return sprite;
}

pixelizer::Image loadImage(std::filesystem::path const& path) {
    s_imageError.clear();
    pixelizer::Image result;
    CCImage image;
    if (!decodePickedImage(path, image)) return result;
    const int w=image.getWidth(), h=image.getHeight();
    if (w<=0 || h<=0) {
        s_imageError = "Image has invalid dimensions.";
        return result;
    }
    if (static_cast<uint64_t>(w)*h>16'000'000) {
        s_imageError = "Image has more than 16 million pixels. Resize to 1920 px or smaller.";
        return result;
    }
    const auto pixels=static_cast<size_t>(w)*h;
    const int n = image.hasAlpha() ? 4 : 3; // CCImage::getDataLen() returns pixel count, not byte count
    if (n!=3 && n!=4) return result;
    const auto len=pixels*n;
    const auto* data=image.getData();
    if (!data) {
        s_imageError = "Image decoder returned no pixel data.";
        return result;
    }
    if (image.getBitsPerComponent() != 8) {
        s_imageError = "Unsupported pixel depth. Convert to a standard 8-bit RGB JPG.";
        return result;
    }
    result.width=w;
    result.height=h;
    result.channels=n;
    result.bytes.assign(data,data+len);
    return result;
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
        auto* sprite=spriteFromPickedImage(m_fields->imagePath);
        if (!sprite) return false;
        const auto reference=pixelizer::convert(rgba,0,0);
        if (reference.columns==0 || reference.rows==0) {
            s_imageError = "Image cannot be converted to a preview. Try a smaller JPG.";
            return false;
        }
        sprite->setPosition(center);
        sprite->setOpacity(180);
        sprite->setScaleX(float(reference.columns*pixelizer::kPixelUnits)/sprite->getContentSize().width);
        sprite->setScaleY(float(reference.rows*pixelizer::kPixelUnits)/sprite->getContentSize().height);
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
        auto result=pixelizer::convert(rgba,center.x,center.y);
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

        addButton(menu,"Import an Image",menu_selector(ImageSchematicPopup::onChooseImage),-82,63,.77f);
        addButton(menu,"Browse Schematics",menu_selector(ImageSchematicPopup::onOpenSchematic),90,63,.76f);
        addButton(menu,"Import",menu_selector(ImageSchematicPopup::onPreview),-104,-36,.82f);
        addButton(menu,"Import Recommended Objects",menu_selector(ImageSchematicPopup::onImportPixels),70,-36,.60f);
        addButton(menu,"Download .schem",menu_selector(ImageSchematicPopup::onDownloadImageSchematic),-93,-83,.75f);
        addButton(menu,"Make a Schematic",menu_selector(ImageSchematicPopup::onSaveSelection),89,-83,.75f);

        m_name=CCLabelBMFont::create("No image selected", "bigFont.fnt");
        m_name->setPosition({195,143});
        m_name->setScale(.49f);
        m_mainLayer->addChild(m_name);
        if (auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get())) {
            if (ui->hasImage()) setFilename(ui->chosenImageName());
        }
        return true;
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

    void onChooseImage(CCObject*) {
        pick::FilePickOptions opts;
        opts.filters.push_back({"Images", {"*.png","*.jpg","*.jpeg","*.webp"}});
        Ref<ImageSchematicPopup> keep(this);
        geode::async::spawn(pick::pick(pick::PickMode::OpenFile,std::move(opts)),[keep](auto result) {
            if (result.isErr()) {message("Image", "The image picker failed.");return;}
            const auto path=result.unwrap();
            if (!path) return;
            if (auto* ui=static_cast<ImageSchematicEditorUI*>(EditorUI::get())) {
                ui->selectImage(*path);
                keep->setFilename(ui->chosenImageName());
            }
        });
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
