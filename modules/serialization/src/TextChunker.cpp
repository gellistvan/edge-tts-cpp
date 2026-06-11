#include "serialization/TextChunker.hpp"
#include "serialization/TextNormalizer.hpp"
#include "serialization/XmlEscaper.hpp"
#include "common/Error.hpp"
#include "common/Utf8.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::serialization {

namespace {

// Strip bytes: space, tab, LF, CR, VT, FF.
constexpr bool is_strip_byte(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return u == 0x20 || u == 0x09 || u == 0x0A || u == 0x0D ||
           u == 0x0B || u == 0x0C;
}

std::string_view strip(std::string_view sv) noexcept {
    while (!sv.empty() && is_strip_byte(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && is_strip_byte(sv.back()))  sv.remove_suffix(1);
    return sv;
}

// If the proposed split would land inside an unterminated &…; entity,
// move split_at to just before the opening '&'.
std::size_t adjust_xml_entity(std::string_view text, std::size_t split_at) noexcept {
    while (split_at > 0) {
        // Find the last '&' strictly before split_at.
        const auto amp = text.rfind('&', split_at - 1);
        if (amp == std::string_view::npos) break;  // no '&' in this prefix

        // Is there a ';' that closes the entity before split_at?
        const auto semi = text.find(';', amp + 1);
        if (semi != std::string_view::npos && semi < split_at) break;  // entity complete

        split_at = amp;  // unterminated entity: move split point to '&'
    }
    return split_at;
}

// Core split loop. Operates on text that is already a UTF-8 string (escaped or raw).
// Returns stripped, non-empty chunks.
std::vector<std::string> split_by_limit(
    std::string_view text,
    std::size_t       limit,
    bool              prefer_newline,
    bool              prefer_space,
    bool              protect_entities)
{
    std::vector<std::string> result;

    while (text.size() > limit) {
        std::size_t split_at = std::string_view::npos;

        // Step 1: prefer \n (sentence/paragraph boundary).
        if (prefer_newline)
            split_at = text.rfind('\n', limit - 1);

        // Step 2: fall back to space (word boundary).
        if (split_at == std::string_view::npos && prefer_space)
            split_at = text.rfind(' ', limit - 1);

        // Step 3: no natural boundary — find the last UTF-8 code-point start
        // at or before `limit`.  Walk back from limit while we see continuation
        // bytes (0x80–0xBF), which cannot be the start of a code point.
        if (split_at == std::string_view::npos) {
            split_at = limit;
            while (split_at > 0 &&
                   common::utf8::is_continuation(text[split_at])) {
                --split_at;
            }
        }

        // Step 4: adjust backward to avoid splitting inside a &…; entity.
        if (protect_entities)
            split_at = adjust_xml_entity(text, split_at);

        // Yield the stripped chunk (silently drop empty chunks).
        const auto ch = strip(text.substr(0, split_at));
        if (!ch.empty())
            result.emplace_back(ch);

        // Advance.  If split_at is 0 (entity or code point larger than limit),
        // advance by 1 to prevent an infinite loop.
        text.remove_prefix(split_at > 0 ? split_at : 1);
    }

    // Remainder.
    const auto rem = strip(text);
    if (!rem.empty())
        result.emplace_back(rem);

    return result;
}

} // namespace

TextChunker::TextChunker(TextChunkerOptions opts) : opts_(opts) {
    if (opts_.max_chunk_size == 0)
        throw std::invalid_argument{"TextChunker: max_chunk_size must be > 0"};
}

common::Result<std::vector<std::string>>
TextChunker::chunk(std::string_view input) const {
    // Step 1: normalize (UTF-8 validation + control-char replacement).
    TextNormalizer normalizer;
    auto norm = normalizer.normalize(input);
    if (!norm)
        return common::Result<std::vector<std::string>>::fail(norm.error());

    if (opts_.size_after_xml_escape) {
        // Reference path: escape first, then split by escaped byte count.
        // The returned chunks are XML-escaped strings ready for SSML embedding.
        const std::string escaped = xml_escape(*norm);
        return common::Result<std::vector<std::string>>::ok(
            split_by_limit(escaped, opts_.max_chunk_size,
                           opts_.prefer_sentence_boundary,
                           opts_.prefer_word_boundary,
                           /*protect_entities=*/true));
    }

    // Non-reference path: split by raw byte count, then escape each chunk.
    // Returned chunks are still XML-escaped, but the size limit was measured
    // before escaping so escaped chunks may exceed max_chunk_size.
    auto raw_chunks = split_by_limit(*norm, opts_.max_chunk_size,
                                     opts_.prefer_sentence_boundary,
                                     opts_.prefer_word_boundary,
                                     /*protect_entities=*/false);
    std::vector<std::string> result;
    result.reserve(raw_chunks.size());
    for (auto& raw : raw_chunks)
        result.push_back(xml_escape(raw));
    return common::Result<std::vector<std::string>>::ok(std::move(result));
}

} // namespace edge_tts::serialization
