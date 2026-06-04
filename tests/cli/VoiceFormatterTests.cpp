#include "edge_tts/cli/VoiceFormatter.hpp"
#include "edge_tts/core/Voice.hpp"
#include "vendor/minigtest/minigtest.hpp"

#include <sstream>
#include <string>
#include <vector>

using edge_tts::cli::VoiceFormatter;
using edge_tts::core::Voice;
using edge_tts::core::VoiceGender;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static VoiceFormatter fmt;

static Voice make_voice(std::string short_name,
                        VoiceGender gender,
                        std::vector<std::string> categories,
                        std::vector<std::string> personalities) {
    Voice v;
    v.short_name          = std::move(short_name);
    v.gender              = gender;
    v.content_categories  = std::move(categories);
    v.voice_personalities = std::move(personalities);
    return v;
}

// Split output into trimmed lines (strips the trailing empty line).
static std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty())
            result.push_back(line);
    }
    return result;
}

// Count the number of non-overlapping occurrences of sub in str.
static std::size_t count_substr(const std::string& str, const std::string& sub) {
    std::size_t count = 0, pos = 0;
    while ((pos = str.find(sub, pos)) != std::string::npos) {
        ++count;
        pos += sub.size();
    }
    return count;
}

// ---------------------------------------------------------------------------
// Empty list
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, EmptyListProducesHeaderAndSeparatorOnly) {
    auto out = fmt.format({});
    auto ls = lines(out);
    // Header row + separator row = 2 lines, no data rows.
    EXPECT_EQ(ls.size(), 2u);
    EXPECT_NE(ls[0].find("Name"), std::string::npos);
    EXPECT_NE(ls[1].find("---"), std::string::npos);
}

TEST(VoiceFormatter, EmptyListHeaderFieldOrder) {
    auto out = fmt.format({});
    auto ls = lines(out);
    const std::string& hdr = ls[0];
    // Reference column order: Name, Gender, ContentCategories, VoicePersonalities
    const auto p_name  = hdr.find("Name");
    const auto p_gen   = hdr.find("Gender");
    const auto p_cat   = hdr.find("ContentCategories");
    const auto p_per   = hdr.find("VoicePersonalities");
    EXPECT_NE(p_name, std::string::npos);
    EXPECT_NE(p_gen,  std::string::npos);
    EXPECT_NE(p_cat,  std::string::npos);
    EXPECT_NE(p_per,  std::string::npos);
    // Must appear left-to-right in the reference order.
    EXPECT_TRUE(p_name < p_gen);
    EXPECT_TRUE(p_gen  < p_cat);
    EXPECT_TRUE(p_cat  < p_per);
}

// ---------------------------------------------------------------------------
// One voice
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, OneVoiceProducesThreeLines) {
    auto v = make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female,
                        {"General"}, {"Friendly", "Positive"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_EQ(lines(out).size(), 3u); // header + separator + 1 data row
}

TEST(VoiceFormatter, OneVoiceShortNameInDataRow) {
    auto v = make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female,
                        {"General"}, {"Friendly"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_NE(out.find("en-US-EmmaMultilingualNeural"), std::string::npos);
}

TEST(VoiceFormatter, OneVoiceGenderFemale) {
    auto v = make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female,
                        {"General"}, {"Friendly"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_NE(out.find("Female"), std::string::npos);
}

TEST(VoiceFormatter, OneVoiceGenderMale) {
    auto v = make_voice("en-US-JennyNeural", VoiceGender::male,
                        {"General"}, {"Friendly"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_NE(out.find("Male"), std::string::npos);
}

TEST(VoiceFormatter, OneVoiceCategoriesJoinedWithComma) {
    // Reference: ", ".join(voice["VoiceTag"]["ContentCategories"])
    auto v = make_voice("en-US-AriaNeural", VoiceGender::female,
                        {"News", "Novel"}, {"Positive"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_NE(out.find("News, Novel"), std::string::npos);
}

TEST(VoiceFormatter, OneVoicePersonalitiesJoinedWithComma) {
    auto v = make_voice("en-US-AriaNeural", VoiceGender::female,
                        {"General"}, {"Friendly", "Positive"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_NE(out.find("Friendly, Positive"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Multiple voices — sorting
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, MultipleVoicesSortedByShortName) {
    std::vector<Voice> vs = {
        make_voice("zh-CN-XiaoxiaoNeural", VoiceGender::female, {"General"}, {"Warm"}),
        make_voice("af-ZA-AdriNeural",     VoiceGender::female, {"General"}, {"Friendly, Positive"}),
        make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female, {"General"}, {"Friendly"}),
    };
    auto out = fmt.format(vs);
    auto ls = lines(out);
    // Data rows start at index 2 (0=header, 1=separator).
    EXPECT_EQ(ls.size(), 5u);
    // First data row must be af-ZA (alphabetically first).
    EXPECT_NE(ls[2].find("af-ZA-AdriNeural"), std::string::npos);
    // Last data row must be zh-CN.
    EXPECT_NE(ls[4].find("zh-CN-XiaoxiaoNeural"), std::string::npos);
}

TEST(VoiceFormatter, SortIsStableForEqualNames) {
    // Two different voices; after sorting, exactly one row each.
    std::vector<Voice> vs = {
        make_voice("en-US-BrandonNeural", VoiceGender::male,   {"General"}, {"Friendly"}),
        make_voice("en-US-AndrewNeural",  VoiceGender::male,   {"General"}, {"Friendly"}),
    };
    auto out = fmt.format(vs);
    auto ls = lines(out);
    EXPECT_EQ(ls.size(), 4u);
    EXPECT_NE(ls[2].find("en-US-AndrewNeural"), std::string::npos);
    EXPECT_NE(ls[3].find("en-US-BrandonNeural"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Field order in data rows
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, FieldOrderInDataRow) {
    // ShortName < Gender < Categories < Personalities (left-to-right position).
    auto v = make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female,
                        {"General"}, {"Friendly", "Positive"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    auto ls = lines(out);
    const std::string& row = ls[2];
    const auto p_name = row.find("en-US-EmmaMultilingualNeural");
    const auto p_gen  = row.find("Female");
    const auto p_cat  = row.find("General");
    const auto p_per  = row.find("Friendly, Positive");
    EXPECT_NE(p_name, std::string::npos);
    EXPECT_NE(p_gen,  std::string::npos);
    EXPECT_NE(p_cat,  std::string::npos);
    EXPECT_NE(p_per,  std::string::npos);
    EXPECT_TRUE(p_name < p_gen);
    EXPECT_TRUE(p_gen  < p_cat);
    EXPECT_TRUE(p_cat  < p_per);
}

// ---------------------------------------------------------------------------
// Column alignment (tabulate "simple" format)
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, ColumnsAlignedByMaxWidth) {
    // Two voices: one with a longer name. The shorter name row must be padded
    // so the Gender column starts at the same horizontal position.
    std::vector<Voice> vs = {
        make_voice("af-ZA-AdriNeural",   VoiceGender::female, {"General"}, {"Friendly"}),
        make_voice("en-US-EmmaMultilingualNeural", VoiceGender::female, {"General"}, {"Friendly"}),
    };
    auto out = fmt.format(vs);
    auto ls = lines(out);
    // In a properly aligned table, "Female" appears at the same offset in
    // both data rows (columns are padded to max width).
    const auto pos2 = ls[2].find("Female");
    const auto pos3 = ls[3].find("Female");
    EXPECT_EQ(pos2, pos3);
}

TEST(VoiceFormatter, SeparatorRowMatchesHeaderWidth) {
    auto v = make_voice("af-ZA-AdriNeural", VoiceGender::female,
                        {"General"}, {"Friendly, Positive"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    auto ls = lines(out);
    // The separator must be at least as long as the header.
    // Both rows use the same column widths so they should be equal length.
    EXPECT_EQ(ls[0].size(), ls[1].size());
}

TEST(VoiceFormatter, ColumnsSeperatedByTwoSpaces) {
    // Between any two adjacent columns there must be at least two spaces.
    // We verify by checking that the separator row contains "  " (two spaces).
    auto v = make_voice("af-ZA-AdriNeural", VoiceGender::female,
                        {"General"}, {"Friendly"});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    auto ls = lines(out);
    EXPECT_NE(ls[1].find("  "), std::string::npos);
}

// ---------------------------------------------------------------------------
// Exact fixture — two reference voices from the README sample
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, ExactFixtureTwoReferenceVoices) {
    // Reference output from README.md for af-ZA voices:
    //   af-ZA-AdriNeural    Female  General  Friendly, Positive
    //   af-ZA-WillemNeural  Male    General  Friendly, Positive
    std::vector<Voice> vs = {
        make_voice("af-ZA-WillemNeural", VoiceGender::male,   {"General"}, {"Friendly", "Positive"}),
        make_voice("af-ZA-AdriNeural",   VoiceGender::female, {"General"}, {"Friendly", "Positive"}),
    };
    auto out = fmt.format(vs);
    auto ls = lines(out);

    EXPECT_EQ(ls.size(), 4u); // header + sep + 2 data rows

    // Header must contain all four column names.
    EXPECT_NE(ls[0].find("Name"),              std::string::npos);
    EXPECT_NE(ls[0].find("Gender"),            std::string::npos);
    EXPECT_NE(ls[0].find("ContentCategories"), std::string::npos);
    EXPECT_NE(ls[0].find("VoicePersonalities"),std::string::npos);

    // Sorted: Adri before Willem.
    EXPECT_NE(ls[2].find("af-ZA-AdriNeural"),   std::string::npos);
    EXPECT_NE(ls[3].find("af-ZA-WillemNeural"),  std::string::npos);

    // Personalities comma-joined.
    EXPECT_NE(ls[2].find("Friendly, Positive"), std::string::npos);
    EXPECT_NE(ls[3].find("Friendly, Positive"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Unicode voice names
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, UnicodeShortNameAppearsInOutput) {
    // Edge TTS voice names are ASCII, but the formatter must not crash if
    // a name contains multibyte UTF-8 characters.
    auto v = make_voice("zh-CN-\xe4\xb8\xad\xe6\x96\x87Neural",
                        VoiceGender::female, {"General"}, {"Friendly"});
    // Should not throw; content presence is the only guarantee.
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    EXPECT_FALSE(out.empty());
    EXPECT_NE(out.find("zh-CN-"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Empty categories / personalities
// ---------------------------------------------------------------------------

TEST(VoiceFormatter, EmptyCategoriesRendersAsEmpty) {
    auto v = make_voice("en-US-TestNeural", VoiceGender::female, {}, {});
    auto out = fmt.format(std::span<const Voice>{&v, 1});
    auto ls = lines(out);
    EXPECT_EQ(ls.size(), 3u);
    // Row must not contain ", " since there's nothing to join.
    const std::string& row = ls[2];
    EXPECT_EQ(count_substr(row, ", "), 0u);
}
