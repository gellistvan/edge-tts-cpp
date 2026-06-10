#include "serialization/MetadataJsonParser.hpp"
#include "serialization/XmlEscaper.hpp"
#include "common/Error.hpp"
#include "core/Chunk.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace edge_tts::serialization {

using nlohmann::json;

common::Result<std::vector<core::BoundaryChunk>>
MetadataJsonParser::parse(std::string_view json_sv) const
{
    json root;
    try {
        root = json::parse(json_sv);
    } catch (const json::parse_error& e) {
        return common::Result<std::vector<core::BoundaryChunk>>::fail(
            {common::ErrorCode::parse_error,
             std::string("malformed metadata JSON: ") + e.what()});
    }

    if (!root.is_object() || !root.contains("Metadata") || !root["Metadata"].is_array())
        return common::Result<std::vector<core::BoundaryChunk>>::fail(
            {common::ErrorCode::parse_error,
             "metadata JSON must be an object with a \"Metadata\" array"});

    std::vector<core::BoundaryChunk> chunks;

    for (const auto& item : root["Metadata"]) {
        if (!item.is_object())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error, "Metadata array element must be an object"});

        if (!item.contains("Type") || !item["Type"].is_string())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error, "Metadata item missing \"Type\" string field"});

        const std::string type = item["Type"].get<std::string>();

        // SessionEnd is silently skipped (reference: `continue`)
        if (type == "SessionEnd") continue;

        if (type != "WordBoundary" && type != "SentenceBoundary") {
            // Forward-compatibility policy: unknown metadata types are silently
            // skipped.  The Edge TTS protocol is reverse-engineered and the
            // service may add new event types (e.g. VisemeBoundary) at any
            // time.  Hard-rejecting them would break synthesis on every
            // protocol update.  The caller is responsible for deciding whether
            // the resulting boundary list is empty (see EdgeProtocolIncoming).
            continue;
        }

        if (!item.contains("Data") || !item["Data"].is_object())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error, "Metadata item missing \"Data\" object"});

        const auto& data = item["Data"];

        if (!data.contains("Offset") || !data["Offset"].is_number_integer())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error,
                 "Metadata Data missing integer \"Offset\" field"});

        if (!data.contains("Duration") || !data["Duration"].is_number_integer())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error,
                 "Metadata Data missing integer \"Duration\" field"});

        if (!data.contains("text") || !data["text"].is_object())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error, "Metadata Data missing \"text\" object"});

        const auto& text_obj = data["text"];
        if (!text_obj.contains("Text") || !text_obj["Text"].is_string())
            return common::Result<std::vector<core::BoundaryChunk>>::fail(
                {common::ErrorCode::parse_error,
                 "Metadata Data.text missing \"Text\" string field"});

        core::BoundaryChunk chunk;
        chunk.type = (type == "WordBoundary")
                   ? core::BoundaryEventType::WordBoundary
                   : core::BoundaryEventType::SentenceBoundary;
        chunk.offset_ticks   = data["Offset"].get<std::int64_t>();
        chunk.duration_ticks = data["Duration"].get<std::int64_t>();
        // XML-unescape the text field, matching reference: unescape(meta_obj["Data"]["text"]["Text"])
        chunk.text = xml_unescape(text_obj["Text"].get<std::string>());

        chunks.push_back(std::move(chunk));
    }

    return common::Result<std::vector<core::BoundaryChunk>>::ok(std::move(chunks));
}

} // namespace edge_tts::serialization
