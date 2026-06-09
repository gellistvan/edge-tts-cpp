#include "edge_tts/api/SpeechSynthesizer.hpp"

int main() {
    edge_tts::api::SpeechSynthesizer tts{"Hello from edge-tts-cpp"};
    tts.save("hello.mp3", "hello.srt");
    return 0;
}
