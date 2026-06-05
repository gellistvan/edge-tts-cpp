#include "edge_tts/api/Communicate.hpp"

int main() {
    edge_tts::api::Communicate tts{"Hello from edge-tts-cpp"};
    tts.save("hello.mp3", "hello.srt");
    return 0;
}
