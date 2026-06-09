#include "common/Error.hpp"
#include "core/Voice.hpp"

int main() {
    // Verify that edge_tts public headers are reachable from a consumer project.
    edge_tts::core::Voice v;
    (void)v;
    return 0;
}
