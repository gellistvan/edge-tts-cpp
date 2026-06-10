#include "cli/PlaybackArguments.hpp"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace edge_tts::cli {

namespace {

bool is_option_token(std::string_view tok) {
    return tok.size() >= 2 && tok[0] == '-';
}

struct SplitResult { std::string_view key; std::string_view value; bool has_value; };
SplitResult split_at_equals(std::string_view tok) {
    const auto pos = tok.find('=');
    if (pos == std::string_view::npos)
        return {tok, {}, false};
    return {tok.substr(0, pos), tok.substr(pos + 1), true};
}

PlaybackParseResult do_parse(const std::vector<std::string>& tokens,
                              const PlaybackArgumentParser& parser) {
    PlaybackArguments args;
    bool seen_text{false}, seen_file{false};

    // Fast-path: --help / -h wins over everything.
    for (const auto& tok : tokens) {
        if (tok == "-h" || tok == "--help") {
            PlaybackParseResult r;
            r.action    = PlaybackParseAction::help;
            r.message   = parser.help_text();
            r.exit_code = 0;
            return r;
        }
    }

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string_view tok = tokens[i];

        // ----------------------------------------------------------------
        // Flags (no value)
        // ----------------------------------------------------------------
        if (tok == "--mpv") {
            args.use_mpv = true;
            continue;
        }

        // ----------------------------------------------------------------
        // Long options with optional '='
        // ----------------------------------------------------------------
        if (tok.substr(0, 2) == "--") {
            auto [key, eq_val, has_eq] = split_at_equals(tok);

            auto require_str = [&](std::string& dest) -> bool {
                if (has_eq) { dest = std::string(eq_val); return true; }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1]))
                    return false;
                dest = tokens[++i];
                return true;
            };
            auto require_opt = [&](std::optional<std::string>& dest) -> bool {
                if (has_eq) { dest = std::string(eq_val); return true; }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1]))
                    return false;
                dest = tokens[++i];
                return true;
            };

            if (key == "--text") {
                if (!require_opt(args.text)) {
                    PlaybackParseResult r;
                    r.message = "--text requires an argument";
                    return r;
                }
                seen_text = true;
            } else if (key == "--file") {
                if (!require_opt(args.file)) {
                    PlaybackParseResult r;
                    r.message = "--file requires an argument";
                    return r;
                }
                seen_file = true;
            } else if (key == "--voice") {
                if (!require_str(args.voice)) {
                    PlaybackParseResult r;
                    r.message = "--voice requires an argument";
                    return r;
                }
            } else if (key == "--rate") {
                if (!require_str(args.rate)) {
                    PlaybackParseResult r;
                    r.message = "--rate requires an argument";
                    return r;
                }
            } else if (key == "--volume") {
                if (!require_str(args.volume)) {
                    PlaybackParseResult r;
                    r.message = "--volume requires an argument";
                    return r;
                }
            } else if (key == "--pitch") {
                if (!require_str(args.pitch)) {
                    PlaybackParseResult r;
                    r.message = "--pitch requires an argument";
                    return r;
                }
            } else if (key == "--proxy") {
                if (!require_opt(args.proxy)) {
                    PlaybackParseResult r;
                    r.message = "--proxy requires an argument";
                    return r;
                }
                if (args.proxy->empty()) {
                    PlaybackParseResult r;
                    r.message = "--proxy URL must not be empty";
                    return r;
                }
                if (args.proxy->find("://") == std::string::npos) {
                    PlaybackParseResult r;
                    r.message = "--proxy URL must include a scheme "
                                "(example: http://host:port)";
                    return r;
                }
            } else if (key == "--write-media" || key == "--write-subtitles" ||
                       key == "--list-voices") {
                // Reference: edge-playback does not expose these options.
                PlaybackParseResult r;
                r.message = std::string(key) + " is not supported by edge-playback";
                return r;
            } else {
                PlaybackParseResult r;
                r.message = "unrecognized option: " + std::string(key);
                return r;
            }
            continue;
        }

        // ----------------------------------------------------------------
        // Short options
        // ----------------------------------------------------------------
        if (tok.size() >= 2 && tok[0] == '-') {
            const char flag = tok[1];

            auto short_opt = [&](std::optional<std::string>& dest) -> bool {
                if (tok.size() > 2) { dest = std::string(tok.substr(2)); return true; }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1]))
                    return false;
                dest = tokens[++i];
                return true;
            };
            auto short_str = [&](std::string& dest) -> bool {
                if (tok.size() > 2) { dest = std::string(tok.substr(2)); return true; }
                if (i + 1 >= tokens.size() || is_option_token(tokens[i + 1]))
                    return false;
                dest = tokens[++i];
                return true;
            };

            switch (flag) {
            case 't':
                if (!short_opt(args.text)) {
                    PlaybackParseResult r;
                    r.message = "-t requires an argument";
                    return r;
                }
                seen_text = true;
                break;
            case 'f':
                if (!short_opt(args.file)) {
                    PlaybackParseResult r;
                    r.message = "-f requires an argument";
                    return r;
                }
                seen_file = true;
                break;
            case 'v':
                if (!short_str(args.voice)) {
                    PlaybackParseResult r;
                    r.message = "-v requires an argument";
                    return r;
                }
                break;
            case 'l':
                // Reference: --list-voices not accepted by edge-playback.
            {
                PlaybackParseResult r;
                r.message = "-l / --list-voices is not supported by edge-playback";
                return r;
            }
            default: {
                PlaybackParseResult r;
                r.message = "unrecognized option: " + std::string(tok);
                return r;
            }
            }
            continue;
        }

        // ----------------------------------------------------------------
        // Positional — not supported
        // ----------------------------------------------------------------
        PlaybackParseResult r;
        r.message = "unexpected positional argument: " + std::string(tok);
        return r;
    }

    // Validate mutually exclusive required group.
    const int group_count = (seen_text ? 1 : 0) + (seen_file ? 1 : 0);

    if (group_count == 0) {
        PlaybackParseResult r;
        r.message = "one of --text/-t or --file/-f is required";
        return r;
    }
    if (group_count > 1) {
        PlaybackParseResult r;
        r.message = "--text and --file are mutually exclusive";
        return r;
    }

    PlaybackParseResult r;
    r.action     = PlaybackParseAction::play;
    r.arguments  = std::move(args);
    r.exit_code  = 0;
    return r;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

PlaybackParseResult PlaybackArgumentParser::parse(int argc, const char* const* argv) const {
    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
        tokens.emplace_back(argv[i]);
    return do_parse(tokens, *this);
}

PlaybackParseResult PlaybackArgumentParser::parse(const std::vector<std::string>& args) const {
    return do_parse(args, *this);
}

std::string PlaybackArgumentParser::help_text(std::string_view program_name) const {
    std::ostringstream ss;
    ss << "Usage: " << program_name << " [OPTIONS]\n"
       << "\n"
       << "Speak text using Microsoft Edge's online text-to-speech API.\n"
       << "See `edge-tts` for additional details on TTS options.\n"
       << "\n"
       << "Options:\n"
       << "  -h, --help          Show this help message and exit\n"
       << "  -t, --text TEXT     What TTS will say\n"
       << "  -f, --file PATH     Same as --text but read from file;\n"
       << "                        use - or /dev/stdin for stdin\n"
       << "  -v, --voice VOICE   Voice for TTS.\n"
       << "                        Default: " << PlaybackArguments::kDefaultVoice << "\n"
       << "      --rate RATE     Set TTS rate. Default +0%.\n"
       << "      --volume VOL    Set TTS volume. Default +0%.\n"
       << "      --pitch PITCH   Set TTS pitch. Default +0Hz.\n"
       << "      --proxy URL     Proxy URL — parsed and validated but not supported\n"
       << "                        at runtime (returns exit 1 if set)\n"
       << "      --mpv           Not supported in this build (only ffplay is available);\n"
       << "                        passing this flag returns an error\n"
       << "\n"
       << "Note: --text and --file are mutually exclusive; exactly one must be provided.\n"
       << "Note: Use --rate=-50% syntax (not --rate -50%) for negative values.\n";
    return ss.str();
}

} // namespace edge_tts::cli
