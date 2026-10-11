#include "MaterialProbe.hpp"
#include <limits>
#include <cstdio>

int main() {
    const auto check=[](bool condition,const char* message){if(!condition){std::fprintf(stderr,"%s\n",message);return false;}return true;};
    bool ok=true;
    ok &= check(MaterialProbeAt(0)==MaterialProbePhase::Baseline,"Line start must retain original material");
    ok &= check(MaterialProbeAt(0.499f)==MaterialProbePhase::Baseline,"Baseline boundary");
    ok &= check(MaterialProbeAt(0.5f)==MaterialProbePhase::PrivateMaterial,"Private window start");
    ok &= check(MaterialProbeAt(1.599f)==MaterialProbePhase::PrivateMaterial,"Private window end");
    ok &= check(MaterialProbeAt(1.6f)==MaterialProbePhase::Restored,"Restore boundary");
    // Simulate a frame jump over the entire private window: do not install it.
    ok &= check(MaterialProbeAt(0.1f)==MaterialProbePhase::Baseline && MaterialProbeAt(2.5f)==MaterialProbePhase::Restored,"Skipped window must not apply material late");
    ok &= check(MaterialProbeAt(-0.1f)==MaterialProbePhase::Invalid,"Negative clock");
    ok &= check(MaterialProbeAt(std::numeric_limits<float>::quiet_NaN())==MaterialProbePhase::Invalid,"NaN clock");
    ok &= check(MaterialProbeAt(std::numeric_limits<float>::infinity())==MaterialProbePhase::Invalid,"Infinite clock");
    return ok ? 0 : 1;
}
