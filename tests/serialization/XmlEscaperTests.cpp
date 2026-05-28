#include "edge_tts/serialization/XmlEscaper.hpp"
#include "vendor/minigtest/minigtest.hpp"

using edge_tts::serialization::xml_escape;
using edge_tts::serialization::xml_unescape;

// ---------------------------------------------------------------------------
// xml_escape — the five XML-sensitive characters
// ---------------------------------------------------------------------------

TEST(XmlEscape, AmpersandBecomesAmpAmp) {
    EXPECT_EQ(xml_escape("&"), "&amp;");
}

TEST(XmlEscape, LessThanBecomesAmpLt) {
    EXPECT_EQ(xml_escape("<"), "&lt;");
}

TEST(XmlEscape, GreaterThanBecomesAmpGt) {
    EXPECT_EQ(xml_escape(">"), "&gt;");
}

TEST(XmlEscape, DoubleQuoteUnchanged) {
    // Python xml.sax.saxutils.escape() does NOT escape double-quotes
    EXPECT_EQ(xml_escape("\""), "\"");
}

TEST(XmlEscape, SingleQuoteUnchanged) {
    // Python xml.sax.saxutils.escape() does NOT escape single-quotes
    EXPECT_EQ(xml_escape("'"), "'");
}

TEST(XmlEscape, MixedCharacters) {
    EXPECT_EQ(xml_escape("a<b>&c\"d'e"), "a&lt;b&gt;&amp;c\"d'e");
}

TEST(XmlEscape, EmptyString) {
    EXPECT_EQ(xml_escape(""), "");
}

TEST(XmlEscape, PlainTextUnchanged) {
    EXPECT_EQ(xml_escape("Hello, world!"), "Hello, world!");
}

TEST(XmlEscape, NotIdempotent_AlreadyEscaped) {
    // Matches Python: escape("&amp;") → "&amp;amp;"
    // The & in &amp; is escaped again.
    EXPECT_EQ(xml_escape("&amp;"), "&amp;amp;");
}

TEST(XmlEscape, UnicodePassedThrough) {
    // Multi-byte sequences are not XML-sensitive; pass through unchanged.
    const std::string emoji = "\xF0\x9F\x98\x80";  // U+1F600 😀
    EXPECT_EQ(xml_escape(emoji), emoji);
}

TEST(XmlEscape, MultipleSpecialCharsInRow) {
    EXPECT_EQ(xml_escape("&&"), "&amp;&amp;");
    EXPECT_EQ(xml_escape("<<"), "&lt;&lt;");
}

// ---------------------------------------------------------------------------
// xml_unescape
// ---------------------------------------------------------------------------

TEST(XmlUnescape, AmpAmpToAmpersand) {
    EXPECT_EQ(xml_unescape("&amp;"), "&");
}

TEST(XmlUnescape, AmpLtToLessThan) {
    EXPECT_EQ(xml_unescape("&lt;"), "<");
}

TEST(XmlUnescape, AmpGtToGreaterThan) {
    EXPECT_EQ(xml_unescape("&gt;"), ">");
}

TEST(XmlUnescape, AmpQuotToDoubleQuote) {
    // Python unescape() handles &quot; even though escape() doesn't produce it
    EXPECT_EQ(xml_unescape("&quot;"), "\"");
}

TEST(XmlUnescape, AmpAposToSingleQuote) {
    // Python unescape() handles &apos;
    EXPECT_EQ(xml_unescape("&apos;"), "'");
}

TEST(XmlUnescape, EmptyString) {
    EXPECT_EQ(xml_unescape(""), "");
}

TEST(XmlUnescape, PlainTextUnchanged) {
    EXPECT_EQ(xml_unescape("Hello"), "Hello");
}

TEST(XmlUnescape, UnknownEntityUnchanged) {
    EXPECT_EQ(xml_unescape("&foo;"), "&foo;");
}

TEST(XmlUnescape, AmpWithNoSemicolon) {
    // Lone & without ; — left as literal &
    EXPECT_EQ(xml_unescape("a&b"), "a&b");
}

TEST(XmlUnescape, DoubleEscapedToSingleEscaped) {
    // xml_unescape("&amp;amp;") → "&amp;" (one unescape step)
    EXPECT_EQ(xml_unescape("&amp;amp;"), "&amp;");
}

TEST(XmlUnescape, MixedEntities) {
    EXPECT_EQ(xml_unescape("&lt;tag&gt;"), "<tag>");
    EXPECT_EQ(xml_unescape("&amp;&lt;&gt;"), "&<>");
}

TEST(XmlUnescape, RoundTrip) {
    // escape then unescape recovers original (for &, <, >)
    const std::string original = "a & b < c > d";
    EXPECT_EQ(xml_unescape(xml_escape(original)), original);
}
