#include <edge_tts/edge_tts.hpp>

int main() {
    edge_tts::api::SpeechSynthesizer tts{"Hello from edge-tts-cpp"};
    auto result = tts.save("hello.mp3", "hello.srt");
    return result ? 0 : 1;
}
