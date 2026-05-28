#pragma once

#include <string>
#include <vector>

namespace edge_tts::core {

struct Voice {
    std::string name;
    std::string short_name;
    std::string gender;
    std::string locale;
    std::vector<std::string> styles;
};

} // namespace edge_tts::core
