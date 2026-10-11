#include "Ramp.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main() {
    Check(BridgeWeight(-1) == 0 && BridgeWeight(0) == 0 && BridgeWeight(3) == 0, "outside clip neutral");
    Check(BridgeWeight(std::numeric_limits<float>::quiet_NaN()) == 0 &&
        BridgeWeight(std::numeric_limits<float>::infinity()) == 0, "invalid clocks neutral");
    Check(std::abs(BridgeWeight(.55f)-.5f) < .0001f && BridgeWeight(1.3f) == 1 && BridgeWeight(1.95f) == 0,
        "half/full/neutral landmarks");
    for (int fps : {30, 60, 120}) {
        for (int frame=0; frame<fps*3; ++frame) {
            float w = BridgeWeight(static_cast<float>(frame)/fps);
            Check(std::isfinite(w) && w >= 0 && w <= 1, "bounded at test rates");
        }
        for (float duration : {3.309f, 4.853f}) {
            Check(NpcBridgeWeight(0, duration) == 0 && NpcBridgeWeight(.5f, duration) == 1 &&
                NpcBridgeWeight(duration, duration) == 0, "NPC neutral/held/released");
            for (int frame = -1; frame < fps * 6; ++frame) {
                float w = NpcBridgeWeight(static_cast<float>(frame) / fps, duration);
                Check(std::isfinite(w) && w >= 0 && w <= 1, "NPC bounded at test rates");
            }
        }
    }
    // A stall, pause and replay cannot accumulate sampler drift: identical audio
    // positions produce identical weights regardless of previous sample order.
    float atHalf = BridgeWeight(.6f);
    for (float t : {1.6f, .6f, .6f, 2.5f, 0.f, 1.3f}) (void)BridgeWeight(t);
    Check(BridgeWeight(.6f) == atHalf, "absolute position after pause/seek/replay");
    std::puts("PASS: bounded ramp, neutral invalid clocks, landmarks, 30/60/120 sample grids, stateless replay. Engine/audio synchronization is NOT tested here.");
}
