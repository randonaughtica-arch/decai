#include "AIDecoPopup.hpp"

using namespace geode::prelude;

AIDecoPopup* AIDecoPopup::create(EditorUI* editorUI) {
    auto ret = new AIDecoPopup();
    if (ret->init(editorUI)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AIDecoPopup::init(EditorUI* editorUI) {
    if (!Popup::init(360.f, 200.f)) return false;

    m_editorUI = editorUI;
    m_context = deco::describeSelection(
        editorUI->getSelectedObjects(),
        Mod::get()->getSettingValue<int64_t>("max-context-objects")
    );

    this->setTitle("AI Deco");

    auto info = CCLabelBMFont::create(
        fmt::format(
            "{} objects selected, {} object types available",
            m_context.totalObjects,
            m_context.idCounts.size() + m_context.extraPalette.size()
        ).c_str(),
        "chatFont.fnt"
    );
    info->setScale(.7f);
    info->setColor({ 200, 200, 200 });
    m_mainLayer->addChildAtPosition(info, Anchor::Top, { 0.f, -48.f });

    m_input = TextInput::create(320.f, "e.g. glowing outline with small dots on top");
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(400);
    m_input->setString(Mod::get()->getSavedValue<std::string>("last-prompt", ""));
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, { 0.f, 5.f });

    m_status = CCLabelBMFont::create("", "bigFont.fnt");
    m_mainLayer->addChildAtPosition(m_status, Anchor::Center, { 0.f, -28.f });

    auto btnSpr = ButtonSprite::create("Generate", "goldFont.fnt", "GJ_button_01.png", .8f);
    m_generateBtn = CCMenuItemSpriteExtra::create(btnSpr, this, menu_selector(AIDecoPopup::onGenerate));
    m_buttonMenu->addChildAtPosition(m_generateBtn, Anchor::Bottom, { 0.f, 28.f });

    if (Mod::get()->getSettingValue<std::string>("api-key").empty()) {
        this->setStatus("Set your Anthropic API key in the mod settings first.", { 255, 120, 120 });
    }
    return true;
}

void AIDecoPopup::setStatus(std::string const& text, ccColor3B color) {
    m_status->setString(text.c_str());
    m_status->setColor(color);
    m_status->limitLabelWidth(320.f, .4f, .2f);
}

void AIDecoPopup::setBusy(bool busy) {
    m_generateBtn->setEnabled(!busy);
    static_cast<ButtonSprite*>(m_generateBtn->getNormalImage())->setColor(
        busy ? ccColor3B{ 120, 120, 120 } : ccColor3B{ 255, 255, 255 }
    );
}

void AIDecoPopup::onGenerate(CCObject*) {
    auto apiKey = Mod::get()->getSettingValue<std::string>("api-key");
    if (apiKey.empty()) {
        this->setStatus("Set your Anthropic API key in the mod settings first.", { 255, 120, 120 });
        return;
    }
    auto request = std::string(m_input->getString());
    if (request.empty()) {
        this->setStatus("Describe the deco you want.", { 255, 120, 120 });
        return;
    }
    Mod::get()->setSavedValue("last-prompt", request);

    size_t maxGenerated = Mod::get()->getSettingValue<int64_t>("max-objects");

    auto body = matjson::makeObject({
        { "model", Mod::get()->getSettingValue<std::string>("model") },
        { "max_tokens", 16000 },
        { "system", deco::buildSystemPrompt(maxGenerated) },
        { "messages", std::vector<matjson::Value>{
            matjson::makeObject({
                { "role", "user" },
                { "content", deco::buildUserPrompt(m_context, request) },
            }),
        } },
        { "output_config", matjson::makeObject({
            { "effort", Mod::get()->getSettingValue<std::string>("effort") },
        }) },
        // If a safety classifier declines, retry on Anthropic's recommended fallback model.
        { "fallbacks", "default" },
    });

    web::WebRequest req;
    req.timeout(std::chrono::seconds(180));
    req.header("x-api-key", apiKey);
    req.header("anthropic-version", "2023-06-01");
    req.header("anthropic-beta", "server-side-fallback-2026-07-01");
    req.header("content-type", "application/json");
    req.bodyJSON(body);

    this->setBusy(true);
    this->setStatus("Generating... (this can take a minute)", { 255, 255, 255 });

    m_request.spawn(
        req.post("https://api.anthropic.com/v1/messages"),
        [this](web::WebResponse res) { this->onResponse(std::move(res)); }
    );
}

void AIDecoPopup::onResponse(web::WebResponse res) {
    this->setBusy(false);
    auto json = res.json().unwrapOr(matjson::Value());

    if (!res.ok()) {
        std::string message = fmt::format("HTTP {}", res.code());
        if (json.contains("error") && json["error"].contains("message")) {
            message = json["error"]["message"].asString().unwrapOr(message);
        }
        log::error("Claude API error: {}", res.string().unwrapOr(""));
        this->setStatus("Error: " + message, { 255, 120, 120 });
        return;
    }

    if (json.contains("stop_reason") && json["stop_reason"].asString().unwrapOr("") == "refusal") {
        this->setStatus("The model declined this request. Try rewording it.", { 255, 120, 120 });
        return;
    }

    std::string text;
    if (json.contains("content") && json["content"].isArray()) {
        for (auto const& block : json["content"].asArray().unwrap()) {
            if (block.contains("type") && block.contains("text")
                && block["type"].asString().unwrapOr("") == "text") {
                text += block["text"].asString().unwrapOr("");
            }
        }
    }
    if (text.empty()) {
        this->setStatus("The AI returned an empty reply.", { 255, 120, 120 });
        return;
    }
    this->applyReply(text);
}

void AIDecoPopup::applyReply(std::string const& text) {
    auto layer = m_editorUI->m_editorLayer;
    auto built = deco::buildObjectString(
        text, m_context, std::max<int>(layer->m_currentLayer, 0),
        Mod::get()->getSettingValue<int64_t>("max-objects")
    );
    if (built.isErr()) {
        log::warn("Unusable AI reply: {}", text);
        this->setStatus(built.unwrapErr(), { 255, 120, 120 });
        return;
    }
    auto result = built.unwrap();

    auto created = layer->createObjectsFromString(result.objectString, true, true);
    if (!created || created->count() == 0) {
        this->setStatus("GD couldn't create the generated objects.", { 255, 120, 120 });
        return;
    }

    // One undo step removes everything that was generated.
    layer->addToUndoList(UndoObject::createWithArray(created, UndoCommand::Paste), false);
    m_editorUI->deselectAll();
    m_editorUI->selectObjects(created, true);
    m_editorUI->updateButtons();

    auto msg = fmt::format("Added {} objects", created->count());
    if (result.skipped > 0) msg += fmt::format(" ({} invalid ones skipped)", result.skipped);
    Notification::create(msg, NotificationIcon::Success)->show();
    this->onClose(nullptr);
}
