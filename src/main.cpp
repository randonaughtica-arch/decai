#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include "AIDecoPopup.hpp"

using namespace geode::prelude;

class $modify(AIDecoEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto spr = EditorButtonSprite::createWithSpriteFrameName(
            "GJ_starsIcon_001.png", 1.f, EditorBaseColor::Cyan
        );
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(AIDecoEditorUI::onAIDeco));
        btn->setID("ai-deco-button"_spr);

        // The right-hand column with Copy / Paste / Edit Object etc. (ID from geode.node-ids).
        if (auto menu = this->getChildByID("editor-buttons-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        } else {
            auto winSize = CCDirector::get()->getWinSize();
            auto fallback = CCMenu::create();
            fallback->setID("ai-deco-menu"_spr);
            fallback->setPosition({ winSize.width - 30.f, winSize.height / 2.f });
            fallback->addChild(btn);
            this->addChild(fallback);
        }
        return true;
    }

    void onAIDeco(CCObject*) {
        auto selected = this->getSelectedObjects();
        if (!selected || selected->count() == 0) {
            FLAlertLayer::create(
                "AI Deco",
                "Select the objects you want decorated first. Include a few deco pieces you like: "
                "the AI can only use object types that are in your selection (or in the mod settings).",
                "OK"
            )->show();
            return;
        }
        AIDecoPopup::create(this)->show();
    }
};
