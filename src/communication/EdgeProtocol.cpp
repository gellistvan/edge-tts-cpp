#include "edge_tts/communication/EdgeProtocol.hpp"
#include "edge_tts/serialization/ProtocolMessage.hpp"
#include "edge_tts/serialization/ProtocolSerializer.hpp"
#include "edge_tts/serialization/SsmlBuilder.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

namespace edge_tts::communication {

// Fixed English abbreviations to avoid locale-dependent strftime output.
static constexpr std::array<const char*, 7> kWeekdays = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static constexpr std::array<const char*, 12> kMonths = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

EdgeProtocol::EdgeProtocol(const common::IClock& clock) noexcept
    : clock_(clock)
{}

std::string EdgeProtocol::format_js_timestamp(
    std::chrono::system_clock::time_point tp)
{
    // Reference: communicate.py date_to_string()
    //   time.strftime("%a %b %d %Y %H:%M:%S GMT+0000 (Coordinated Universal Time)",
    //                 time.gmtime())
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm gmt{};
#if defined(_WIN32)
    gmtime_s(&gmt, &t);
#else
    gmtime_r(&t, &gmt);
#endif

    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "%s %s %02d %04d %02d:%02d:%02d GMT+0000 (Coordinated Universal Time)",
        kWeekdays[static_cast<std::size_t>(gmt.tm_wday)],
        kMonths[static_cast<std::size_t>(gmt.tm_mon)],
        gmt.tm_mday,
        gmt.tm_year + 1900,
        gmt.tm_hour,
        gmt.tm_min,
        gmt.tm_sec);
    return buf;
}

common::Result<std::string> EdgeProtocol::build_speech_config_frame(
    const core::TtsConfig&    config,
    const ConnectionMetadata& /*metadata*/) const
{
    // Reference: communicate.py send_command_request()
    //   word_boundary = self.tts_config.boundary == "WordBoundary"
    //   wd = "true" if word_boundary else "false"
    //   sq = "true" if not word_boundary else "false"
    const bool word_boundary = (config.boundary_type == core::BoundaryType::word);
    const char* wd = word_boundary ? "true" : "false";
    const char* sq = word_boundary ? "false" : "true";

    const std::string timestamp = format_js_timestamp(clock_.now());

    // Build JSON body.
    // Reference JSON (Python concatenation):
    //   '{"context":{"synthesis":{"audio":{"metadataoptions":{'
    //   f'"sentenceBoundaryEnabled":"{sq}","wordBoundaryEnabled":"{wd}"'
    //   "},"
    //   '"outputFormat":"audio-24khz-48kbitrate-mono-mp3"'
    //   "}}}}\r\n"
    //
    // Note: boundary values are JSON strings ("true"/"false"), not booleans.
    // Note: body includes trailing \r\n — matches the Python reference exactly.
    char body_buf[256];
    std::snprintf(body_buf, sizeof(body_buf),
        "{\"context\":{\"synthesis\":{\"audio\":{\"metadataoptions\":{"
        "\"sentenceBoundaryEnabled\":\"%s\",\"wordBoundaryEnabled\":\"%s\"},"
        "\"outputFormat\":\"%s\"}}}}\r\n",
        sq, wd,
        std::string(config.output_format.value()).c_str());

    serialization::ProtocolMessage msg;
    msg.headers.emplace_back("X-Timestamp",   timestamp);
    msg.headers.emplace_back("Content-Type",  "application/json; charset=utf-8");
    msg.headers.emplace_back("Path",          "speech.config");
    msg.body = body_buf;

    serialization::ProtocolSerializer serializer;
    return common::Result<std::string>::ok(serializer.serialize(msg));
}

common::Result<std::string> EdgeProtocol::build_ssml_frame(
    const core::TtsConfig&    config,
    std::string_view          text_chunk,
    const ConnectionMetadata& metadata) const
{
    // Reference: communicate.py ssml_headers_plus_data() + send_ssml_request()
    //   ssml_headers_plus_data(connect_id(), date_to_string(), mkssml(...))
    //
    // text_chunk arrives pre-escaped from serialization::TextChunker (via
    // api::SpeechSynthesizer → SynthesisSession).  build_from_escaped_text embeds it
    // verbatim so escaping happens exactly once across the full pipeline.
    serialization::SsmlBuilder builder;
    auto ssml_result = builder.build_from_escaped_text(config, text_chunk);
    if (!ssml_result)
        return common::Result<std::string>::fail(std::move(ssml_result.error()));

    // Reference ssml_headers_plus_data():
    //   f"X-RequestId:{request_id}\r\n"
    //   "Content-Type:application/ssml+xml\r\n"
    //   f"X-Timestamp:{timestamp}Z\r\n"   ← trailing Z (documented Edge bug)
    //   "Path:ssml\r\n\r\n"
    //   f"{ssml}"
    const std::string timestamp = format_js_timestamp(clock_.now()) + "Z";

    serialization::ProtocolMessage msg;
    msg.headers.emplace_back("X-RequestId",  metadata.request_id);
    msg.headers.emplace_back("Content-Type", "application/ssml+xml");
    msg.headers.emplace_back("X-Timestamp",  timestamp);
    msg.headers.emplace_back("Path",         "ssml");
    msg.body = std::move(*ssml_result);

    serialization::ProtocolSerializer serializer;
    return common::Result<std::string>::ok(serializer.serialize(msg));
}

} // namespace edge_tts::communication
