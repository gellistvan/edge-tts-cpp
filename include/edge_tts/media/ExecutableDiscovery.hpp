#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace edge_tts::media {

// Locates a named executable by scanning a PATH-style environment variable.
//
// Reference: edge_playback/__main__.py _check_deps() uses shutil.which() to
// locate "mpv" and "edge-tts" before launching them.  This class replicates
// that scan without executing any process, making it testable with temp dirs.
class ExecutableDiscovery {
public:
    // Search each directory in path_env (colon-separated on POSIX,
    // semicolon-separated on Windows) for an entry named executable that is a
    // regular file with execute permission.
    //
    // On Windows (or when the host is determined to be Windows at compile time)
    // the search also tries the name with a ".exe" suffix appended if the
    // caller did not already provide one.
    //
    // Returns the first matching absolute path, or std::nullopt if not found.
    [[nodiscard]] std::optional<std::filesystem::path>
    find_on_path(std::string_view executable, std::string_view path_env) const;
};

} // namespace edge_tts::media
