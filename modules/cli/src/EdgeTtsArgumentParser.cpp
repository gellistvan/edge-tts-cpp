#include "cli/EdgeTtsArgumentParser.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::cli {

namespace {

// -------------------------------------------------------------------------
// Token-level helpers
// -------------------------------------------------------------------------

// Returns true when the token looks like a flag or option (starts with "-").
bool is_option_token(std::string_view tok) {
    return tok.size() >= 2 && tok[0] == '-';
}

// Try to extract a value attached via '=' (e.g. "--rate=+0%").
// Returns {key, value, true} on success; {tok, "", false} if no '=' found.
struct SplitResult { std::string_view key; std::string_view value; bool has_value; };
SplitResult split_at_equals(std::string_view tok) {
    const auto pos = tok.find('=');
    if (pos == std::string_view::npos)
        return {tok, {}, false};
    return {tok.substr(0, pos), tok.substr(pos + 1), true};
}

// -------------------------------------------------------------------------
// Parser core
// -------------------------------------------------------------------------

ParseResult do_parse(const std::vector<std::string>& tokens, const EdgeTtsArgumentParser& parser) {
    EdgeTtsArguments args;
    bool seen_text{false}, seen_file{false}, seen_list_voices{false};

    // Fast-path: help and version are handled before anything else so they
    // work even when other options are invalid.
    for (const auto& tok : tokens) {
        if (tok == "-h" || tok == "--help") {
            ParseResult r;
            r.action    = ParseAction::help;
            r.message   = parser.help_text();
            r.exit_code = 0;
            return r;
        }
        if (tok == "--version") {
            ParseResult r;
            r.action    = ParseAction::version;
            r.message   = EdgeTtsArgumentParser::version_string();
            r.exit_code = 0;
            return r;
        }
    }

    // Main parse loop.
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string_view tok = tokens[i];

        // -----------------------------------------------------------------
        // Flags (no value)
        // -----------------------------------------------------------------
        if (tok == "-l" || tok == "--list-voices") {
            seen_list_voices = true;
            args.list_voices = true;
            continue;
        }

        // -----------------------------------------------------------------
        // Options with a value — long form with '='
        // -----------------------------------------------------------------
        if (tok.substr(0, 2) == "--") {
            auto [key, eq_val, has_eq] = split_at_equals(tok);

            auto require_value = [&](std::string& dest) -> bool {
                if (has_eq) {
                    dest = std::string(eq_val);
                    return true;
                }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1])) {
                    return false;
                }
                dest = tokens[++i];
                return true;
            };
            auto require_opt_value = [&](std::optional<std::string>& dest) -> bool {
                if (has_eq) {
                    dest = std::string(eq_val);
                    return true;
                }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1])) {
                    return false;
                }
                dest = tokens[++i];
                return true;
            };

            if (key == "--text") {
                if (!require_opt_value(args.text)) {
                    ParseResult r;
                    r.message = "--text requires an argument";
                    return r;
                }
                seen_text = true;
            } else if (key == "--file") {
                if (!require_opt_value(args.file)) {
                    ParseResult r;
                    r.message = "--file requires an argument";
                    return r;
                }
                seen_file = true;
            } else if (key == "--voice") {
                if (!require_value(args.voice)) {
                    ParseResult r;
                    r.message = "--voice requires an argument";
                    return r;
                }
            } else if (key == "--rate") {
                if (!require_value(args.rate)) {
                    ParseResult r;
                    r.message = "--rate requires an argument";
                    return r;
                }
            } else if (key == "--volume") {
                if (!require_value(args.volume)) {
                    ParseResult r;
                    r.message = "--volume requires an argument";
                    return r;
                }
            } else if (key == "--pitch") {
                if (!require_value(args.pitch)) {
                    ParseResult r;
                    r.message = "--pitch requires an argument";
                    return r;
                }
            } else if (key == "--write-media") {
                if (!require_opt_value(args.write_media)) {
                    ParseResult r;
                    r.message = "--write-media requires an argument";
                    return r;
                }
            } else if (key == "--write-subtitles") {
                if (!require_opt_value(args.write_subtitles)) {
                    ParseResult r;
                    r.message = "--write-subtitles requires an argument";
                    return r;
                }
            } else if (key == "--proxy") {
                if (!require_opt_value(args.proxy)) {
                    ParseResult r;
                    r.message = "--proxy requires an argument";
                    return r;
                }
                // Basic format validation — catches obviously invalid values at
                // parse time so the exit code is 2 rather than a runtime 1.
                if (args.proxy->empty()) {
                    ParseResult r;
                    r.message = "--proxy URL must not be empty";
                    return r;
                }
                if (args.proxy->find("://") == std::string::npos) {
                    ParseResult r;
                    r.message = "--proxy URL must include a scheme "
                                "(example: http://host:port)";
                    return r;
                }
            } else {
                ParseResult r;
                r.message = "unrecognized option: " + std::string(key);
                return r;
            }
            continue;
        }

        // -----------------------------------------------------------------
        // Short options
        // -----------------------------------------------------------------
        if (tok.size() >= 2 && tok[0] == '-') {
            const char flag = tok[1];

            // Value attached directly (e.g. "-tHello") or as next token.
            auto short_value = [&](std::optional<std::string>& dest) -> bool {
                if (tok.size() > 2) {
                    dest = std::string(tok.substr(2));
                    return true;
                }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1])) {
                    return false;
                }
                dest = tokens[++i];
                return true;
            };
            auto short_value_str = [&](std::string& dest) -> bool {
                if (tok.size() > 2) {
                    dest = std::string(tok.substr(2));
                    return true;
                }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1])) {
                    return false;
                }
                dest = tokens[++i];
                return true;
            };

            switch (flag) {
            case 't':
                if (!short_value(args.text)) {
                    ParseResult r;
                    r.message = "-t requires an argument";
                    return r;
                }
                seen_text = true;
                break;
            case 'f':
                if (!short_value(args.file)) {
                    ParseResult r;
                    r.message = "-f requires an argument";
                    return r;
                }
                seen_file = true;
                break;
            case 'v':
                if (!short_value_str(args.voice)) {
                    ParseResult r;
                    r.message = "-v requires an argument";
                    return r;
                }
                break;
            default: {
                ParseResult r;
                r.message = "unrecognized option: " + std::string(tok);
                return r;
            }
            }
            continue;
        }

        // -----------------------------------------------------------------
        // Positional — not supported
        // -----------------------------------------------------------------
        ParseResult r;
        r.message = "unexpected positional argument: " + std::string(tok);
        return r;
    }

    // -------------------------------------------------------------------------
    // Validate mutually exclusive required group.
    // -------------------------------------------------------------------------
    const int group_count =
        (seen_text ? 1 : 0) + (seen_file ? 1 : 0) + (seen_list_voices ? 1 : 0);

    if (group_count == 0) {
        ParseResult r;
        r.message = "one of --text/-t, --file/-f, or --list-voices/-l is required";
        return r;
    }
    if (group_count > 1) {
        ParseResult r;
        r.message = "--text, --file, and --list-voices are mutually exclusive";
        return r;
    }

    // Success.
    ParseResult r;
    r.arguments  = std::move(args);
    r.exit_code  = 0;

    if (seen_list_voices)
        r.action = ParseAction::list_voices;
    else
        r.action = ParseAction::synthesize;

    return r;
}

} // namespace

// -------------------------------------------------------------------------
// Public interface
// -------------------------------------------------------------------------

ParseResult EdgeTtsArgumentParser::parse(int argc, const char* const* argv) const {
    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
        tokens.emplace_back(argv[i]);
    return do_parse(tokens, *this);
}

ParseResult EdgeTtsArgumentParser::parse(const std::vector<std::string>& args) const {
    return do_parse(args, *this);
}

std::string EdgeTtsArgumentParser::version_string() {
    return "edge-tts-cpp 0.1.0";
}

std::string EdgeTtsArgumentParser::help_text(std::string_view program_name) const {
    std::ostringstream ss;
    ss << "Usage: " << program_name << " [OPTIONS]\n"
       << "\n"
       << "Text-to-speech using Microsoft Edge's online TTS service.\n"
       << "\n"
       << "Options:\n"
       << "  -h, --help                    Show this help message and exit\n"
       << "      --version                 Show version information and exit\n"
       << "  -t, --text TEXT               What TTS will say\n"
       << "  -f, --file PATH               Same as --text but read from file;\n"
       << "                                  use - or /dev/stdin for stdin\n"
       << "  -l, --list-voices             List available voices and exit\n"
       << "  -v, --voice VOICE             Voice for TTS.\n"
       << "                                  Default: " << EdgeTtsArguments::kDefaultVoice << "\n"
       << "      --rate RATE               Set TTS rate. Default +0%.\n"
       << "      --volume VOL              Set TTS volume. Default +0%.\n"
       << "      --pitch PITCH             Set TTS pitch. Default +0Hz.\n"
       << "      --write-media PATH        Send media output to file instead of stdout\n"
       << "      --write-subtitles PATH    Send subtitle output to file;\n"
       << "                                  use - to send to stderr\n"
       << "      --proxy URL               Proxy URL — parsed and validated but not\n"
       << "                                  supported at runtime (returns exit 1 if set)\n"
       << "\n"
       << "Note: --text, --file, and --list-voices are mutually exclusive;\n"
       << "      exactly one must be provided.\n"
       << "\n"
       << "Note: Use --rate=-50% syntax (not --rate -50%) for negative values.\n";
    return ss.str();
}

} // namespace edge_tts::cli
