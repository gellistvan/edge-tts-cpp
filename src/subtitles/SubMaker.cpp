#include "edge_tts/subtitles/SubMaker.hpp"
#include "edge_tts/subtitles/SrtComposer.hpp"
#include "edge_tts/subtitles/SubtitleTime.hpp"
#include "edge_tts/common/Error.hpp"

#include <span>
#include <string>
#include <vector>

namespace edge_tts::subtitles {

common::Result<void> SubMaker::feed(const core::BoundaryChunk& boundary)
{
    // Enforce type consistency (reference: ValueError on type mismatch)
    if (type_.has_value() && *type_ != boundary.type)
        return common::Result<void>::fail(
            {common::ErrorCode::invalid_argument,
             "mixed boundary types: all fed boundaries must be the same type"});

    // Convert ticks → SubtitleTime (rejects negative ticks)
    auto start_r = SubtitleTime::from_edge_ticks(boundary.offset_ticks);
    if (!start_r)
        return common::Result<void>::fail(start_r.error());

    const std::int64_t end_ticks = boundary.offset_ticks + boundary.duration_ticks;
    auto end_r = SubtitleTime::from_edge_ticks(end_ticks);
    if (!end_r)
        return common::Result<void>::fail(end_r.error());

    if (!type_.has_value())
        type_ = boundary.type;

    // Text is already XML-unescaped by MetadataJsonParser — store verbatim.
    cues_.push_back({*start_r, *end_r, boundary.text});
    return common::Result<void>::ok();
}

std::vector<SubtitleCue> SubMaker::cues() const
{
    return cues_;
}

common::Result<std::string> SubMaker::to_srt() const
{
    SrtComposer composer;
    return composer.compose(std::span<const SubtitleCue>{cues_});
}

void SubMaker::clear() noexcept
{
    cues_.clear();
    type_.reset();
}

} // namespace edge_tts::subtitles
