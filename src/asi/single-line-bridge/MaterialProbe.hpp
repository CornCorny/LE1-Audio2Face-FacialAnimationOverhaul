#pragma once
#include <cmath>

enum class MaterialProbePhase { Invalid, Baseline, PrivateMaterial, Restored };
// Sample the dialogue audio clock directly. A late tick must never apply the
// private material after its test window; pauses naturally hold the same phase.
inline MaterialProbePhase MaterialProbeAt(float audioTime) {
    if (!std::isfinite(audioTime) || audioTime<0) return MaterialProbePhase::Invalid;
    if (audioTime<0.5f) return MaterialProbePhase::Baseline;
    if (audioTime<1.6f) return MaterialProbePhase::PrivateMaterial;
    return MaterialProbePhase::Restored;
}
