#include "edge_tts/media/ExecutableDiscovery.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace edge_tts::media {

namespace {

// PATH separator: ':' on POSIX, ';' on Windows.
#ifdef _WIN32
constexpr char k_path_sep = ';';
#else
constexpr char k_path_sep = ':';
#endif

bool is_executable_file(const std::filesystem::path& p) {
    std::error_code ec;
    auto status = std::filesystem::status(p, ec);
    if (ec || !std::filesystem::is_regular_file(status))
        return false;

#ifdef _WIN32
    // On Windows any regular file is considered "executable" here; the OS
    // checks the PE header at launch time.
    return true;
#else
    using perms = std::filesystem::perms;
    auto perm = status.permissions();
    return (perm & (perms::owner_exec | perms::group_exec | perms::others_exec))
           != perms::none;
#endif
}

std::optional<std::filesystem::path>
probe(const std::filesystem::path& dir, std::string_view name) {
    auto candidate = dir / name;
    if (is_executable_file(candidate))
        return candidate;

#ifdef _WIN32
    // Try appending .exe only when the name does not already end with it.
    std::string lower{name};
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".exe") {
        auto with_exe = dir / (std::string{name} + ".exe");
        if (is_executable_file(with_exe))
            return with_exe;
    }
#endif

    return std::nullopt;
}

} // anonymous namespace

std::optional<std::filesystem::path>
ExecutableDiscovery::find_on_path(std::string_view executable,
                                  std::string_view path_env) const {
    std::string_view remaining = path_env;
    while (!remaining.empty()) {
        auto sep = remaining.find(k_path_sep);
        std::string_view segment = (sep == std::string_view::npos)
                                       ? remaining
                                       : remaining.substr(0, sep);

        if (!segment.empty()) {
            if (auto found = probe(std::filesystem::path{segment}, executable))
                return found;
        }

        if (sep == std::string_view::npos)
            break;
        remaining = remaining.substr(sep + 1);
    }
    return std::nullopt;
}

} // namespace edge_tts::media
