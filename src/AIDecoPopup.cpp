#include "AIDecoPopup.hpp"
#include <optional>

using namespace geode::prelude;

namespace {

struct Provider {
    std::string url;
    std::string model;
    std::string apiKey;
    std::string keyHelp; // where to get a key, empty if none is needed
    bool isClaude = false;
    bool isLocal = false;

    std::optional<std::string> setupProblem() const {
        if (!keyHelp.empty() && apiKey.empty()) {
            return "Add an API key in the mod settings. " + keyHelp;
        }
        return std::nullopt;
    }
};

Provider currentProvider() {
    auto name = Mod::get()->getSettingValue<std::string>("provider");
    auto model = Mod::get()->getSettingValue<std::string>("free-model");

    Provider p;
    if (name == "Claude") {
        p.url = "https://api.anthropic.com/v1/messages";
        p.model = Mod::get()->getSettingValue<std::string>("model");
        p.apiKey = Mod::get()->getSettingValue<std::string>("api-key");
        p.keyHelp = "Get one at console.anthropic.com";
        p.isClaude = true;
        return p;
    }
    if (name == "Groq") {
        p.url = "https://api.groq.com/openai/v1/chat/completions";
        p.model = "openai/gpt-oss-120b";
        p.keyHelp = "Free key: console.groq.com";
    }
    else if (name == "OpenRouter") {
        p.url = "https://openrouter.ai/api/v1/chat/completions";
        p.model = "openrouter/free";
        p.keyHelp = "Free key: openrouter.ai/keys";
    }
    else if (name == "Ollama") {
        p.url = Mod::get()->getSettingValue<std::string>("ollama-url") + "/v1/chat/completions";
        p.model = "qwen2.5:7b";
        p.isLocal = true;
    }
    else {
        p.url = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions";
        p.model = "gemini-2.5-flash";
        p.keyHelp = "Free key: aistudio.google.com/apikey";
    }
    if (!model.empty()) p.model = model;
    p.apiKey = Mod::get()->getSettingValue<std::string>("free-api-key");
    return p;
}

}

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

    if (auto problem = currentProvider().setupProblem()) {
        this->setStatus(*problem, { 255, 120, 120 });
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
    auto provider = currentProvider();
    if (auto problem = provider.setupProblem()) {
        this->setStatus(*problem, { 255, 120, 120 });
        return;
    }
    auto request = std::string(m_input->getString());
    if (request.empty()) {
        this->setStatus("Describe the deco you want.", { 255, 120, 120 });
        return;
    }
    Mod::get()->setSavedValue("last-prompt", request);

    size_t maxGenerated = Mod::get()->getSettingValue<int64_t>("max-objects");
    auto system = deco::buildSystemPrompt(maxGenerated);
    auto user = deco::buildUserPrompt(m_context, request);

    web::WebRequest req;
    req.timeout(std::chrono::seconds(provider.isLocal ? 600 : 180));
    req.header("content-type", "application/json");

    if (provider.isClaude) {
        req.header("x-api-key", provider.apiKey);
        req.header("anthropic-version", "2023-06-01");
        req.header("anthropic-beta", "server-side-fallback-2026-07-01");
        req.bodyJSON(matjson::makeObject({
            { "model", provider.model },
            { "max_tokens", 16000 },
            { "system", system },
            { "messages", std::vector<matjson::Value>{
                matjson::makeObject({ { "role", "user" }, { "content", user } }),
            } },
            { "output_config", matjson::makeObject({
                { "effort", Mod::get()->getSettingValue<std::string>("effort") },
            }) },
            // If a safety classifier declines, retry on Anthropic's recommended fallback model.
            { "fallbacks", "default" },
        }));
    }
    else {
        // Gemini, Groq, OpenRouter and Ollama all accept the OpenAI chat completions format.
        if (!provider.apiKey.empty()) {
            req.header("Authorization", "Bearer " + provider.apiKey);
        }
        req.bodyJSON(matjson::makeObject({
            { "model", provider.model },
            { "messages", std::vector<matjson::Value>{
                matjson::makeObject({ { "role", "system" }, { "content", system } }),
                matjson::makeObject({ { "role", "user" }, { "content", user } }),
            } },
        }));
    }

    this->setBusy(true);
    this->setStatus(
        provider.isLocal ? "Generating locally... (this can take several minutes)" : "Generating... (this can take a minute)",
        { 255, 255, 255 }
    );

    m_usingClaude = provider.isClaude;
    m_request.spawn(
        req.post(provider.url),
        [this](web::WebResponse res) { this->onResponse(std::move(res)); }
    );
}

void AIDecoPopup::onResponse(web::WebResponse res) {
    this->setBusy(false);
    auto json = res.json().unwrapOr(matjson::Value());

    if (!res.ok()) {
        std::string message = res.code() == 0
            ? "Couldn't connect. Check your internet, or that Ollama is running."
            : fmt::format("HTTP {}", res.code());
        if (json.contains("error") && json["error"].isObject() && json["error"].contains("message")) {
            message = json["error"]["message"].asString().unwrapOr(message);
        }
        if (res.code() == 429) {
            message = "Rate limited by the AI service. Wait a minute and try again.";
        }
        log::error("AI API error {}: {}", res.code(), res.string().unwrapOr(""));
        this->setStatus("Error: " + message, { 255, 120, 120 });
        return;
    }

    std::string text;
    if (m_usingClaude) {
        if (json.contains("stop_reason") && json["stop_reason"].asString().unwrapOr("") == "refusal") {
            this->setStatus("The model declined this request. Try rewording it.", { 255, 120, 120 });
            return;
        }
        if (json.contains("content") && json["content"].isArray()) {
            for (auto const& block : json["content"].asArray().unwrap()) {
                if (block.contains("type") && block.contains("text")
                    && block["type"].asString().unwrapOr("") == "text") {
                    text += block["text"].asString().unwrapOr("");
                }
            }
        }
    }
    else if (json.contains("choices") && json["choices"].isArray()) {
        auto const& choices = json["choices"].asArray().unwrap();
        if (!choices.empty() && choices[0].contains("message") && choices[0]["message"].contains("content")) {
            text = choices[0]["message"]["content"].asString().unwrapOr("");
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
