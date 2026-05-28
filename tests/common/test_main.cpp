#ifdef EDGE_TTS_NO_GTEST
int main() { return 0; }
#else
#include <gtest/gtest.h>
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
#endif
