#include "serialization/VoiceJsonParser.hpp"
#include "common/Error.hpp"
#include "core/Voice.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace edge_tts::serialization {

using nlohmann::json;

static common::Result<std::string> require_string(
    const json& obj, const char* key)
{
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return common::Result<std::string>::fail(
            {common::ErrorCode::parse_error,
             std::string("voice entry missing required field: ") + key});
    return common::Result<std::string>::ok(it->get<std::string>());
}

common::Result<std::vector<core::Voice>>
VoiceJsonParser::parse(std::string_view json_sv) const
{
    json root;
    try {
        root = json::parse(json_sv);
    } catch (const json::parse_error& e) {
        return common::Result<std::vector<core::Voice>>::fail(
            {common::ErrorCode::parse_error,
             std::string("malformed JSON: ") + e.what()});
    }

    if (!root.is_array()) {
        return common::Result<std::vector<core::Voice>>::fail(
            {common::ErrorCode::parse_error, "voice list JSON root must be an array"});
    }

    std::vector<core::Voice> voices;
    voices.reserve(root.size());

    for (const auto& entry : root) {
        if (!entry.is_object()) {
            return common::Result<std::vector<core::Voice>>::fail(
                {common::ErrorCode::parse_error,
                 "voice list JSON array element must be an object"});
        }

        core::Voice v;

        auto name = require_string(entry, "Name");
        if (!name) return common::Result<std::vector<core::Voice>>::fail(name.error());
        v.name = std::move(*name);

        auto short_name = require_string(entry, "ShortName");
        if (!short_name) return common::Result<std::vector<core::Voice>>::fail(short_name.error());
        v.short_name = std::move(*short_name);

        auto gender_str = require_string(entry, "Gender");
        if (!gender_str) return common::Result<std::vector<core::Voice>>::fail(gender_str.error());
        auto gender = core::voice_gender_from_string(*gender_str);
        if (!gender)
            return common::Result<std::vector<core::Voice>>::fail(
                {common::ErrorCode::parse_error,
                 "voice entry has unrecognised Gender value: " + std::string(*gender_str)});
        v.gender = *gender;

        auto locale = require_string(entry, "Locale");
        if (!locale) return common::Result<std::vector<core::Voice>>::fail(locale.error());
        v.locale = std::move(*locale);

        auto codec = require_string(entry, "SuggestedCodec");
        if (!codec) return common::Result<std::vector<core::Voice>>::fail(codec.error());
        v.suggested_codec = std::move(*codec);

        auto friendly = require_string(entry, "FriendlyName");
        if (!friendly) return common::Result<std::vector<core::Voice>>::fail(friendly.error());
        v.friendly_name = std::move(*friendly);

        auto status = require_string(entry, "Status");
        if (!status) return common::Result<std::vector<core::Voice>>::fail(status.error());
        v.status = std::move(*status);

        // VoiceTag is optional; missing sub-lists default to empty.
        if (auto tag_it = entry.find("VoiceTag");
            tag_it != entry.end() && tag_it->is_object())
        {
            const auto& tag = *tag_it;
            if (auto cc_it = tag.find("ContentCategories");
                cc_it != tag.end() && cc_it->is_array())
            {
                for (const auto& c : *cc_it)
                    if (c.is_string()) v.content_categories.push_back(c.get<std::string>());
            }
            if (auto vp_it = tag.find("VoicePersonalities");
                vp_it != tag.end() && vp_it->is_array())
            {
                for (const auto& p : *vp_it)
                    if (p.is_string()) v.voice_personalities.push_back(p.get<std::string>());
            }
        }

        // Derive language from locale prefix before the first '-'.
        const auto dash = v.locale.find('-');
        v.language = (dash != std::string::npos) ? v.locale.substr(0, dash) : v.locale;

        voices.push_back(std::move(v));
    }

    return common::Result<std::vector<core::Voice>>::ok(std::move(voices));
}

} // namespace edge_tts::serialization
