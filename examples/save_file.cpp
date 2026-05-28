#include "edge_tts/communication/Communicate.hpp"

int main() {
    edge_tts::communication::Communicate tts{"Hello from edge-tts-cpp"};
    tts.save("hello.mp3", "hello.srt");
    return 0;
}
