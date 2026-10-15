#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include "DecoGen.hpp"

class AIDecoPopup : public geode::Popup {
protected:
    EditorUI* m_editorUI = nullptr;
    deco::SelectionContext m_context;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_generateBtn = nullptr;
    bool m_usingClaude = false;
    // Cancels the request automatically if the popup is closed first.
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;

    bool init(EditorUI* editorUI);
    void onGenerate(cocos2d::CCObject*);
    void onResponse(geode::utils::web::WebResponse res);
    void applyReply(std::string const& text);
    void setStatus(std::string const& text, cocos2d::ccColor3B color);
    void setBusy(bool busy);

public:
    static AIDecoPopup* create(EditorUI* editorUI);
};
