#pragma once

#include <string>
#include <string_view>

namespace edge_tts::serialization {

// Escapes the five XML-sensitive characters in raw, producing a string safe
// for embedding inside an XML element or attribute value.
//
// Python reference: xml.sax.saxutils.escape() (communicate.py line 350)
//
// Mapping (identical to Python):
//   &  →  &amp;
//   <  →  &lt;
//   >  →  &gt;
//   "  →  " (unchanged — Python escape() does NOT quote double-quotes)
//   '  →  ' (unchanged — Python escape() does NOT quote single-quotes)
//
// Note: xml_escape is NOT idempotent.  Calling it twice double-escapes:
//   xml_escape("&amp;")  →  "&amp;amp;"
//   This matches the Python behavior exactly.
[[nodiscard]] std::string xml_escape(std::string_view raw);

// Reverses the five standard XML/HTML entity references.
//
// Python reference: xml.sax.saxutils.unescape() (communicate.py line 398)
//
// Mapping:
//   &amp;  →  &
//   &lt;   →  <
//   &gt;   →  >
//   &quot; →  "
//   &apos; →  '
//
// Unknown or malformed entities are left unchanged.
[[nodiscard]] std::string xml_unescape(std::string_view escaped);

} // namespace edge_tts::serialization
