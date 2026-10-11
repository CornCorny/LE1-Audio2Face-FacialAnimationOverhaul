#pragma once
#include <cmath>

// Hold the NPC mouth stretch through the camera cut; release before voice end.
inline float NpcBridgeWeight(float t, float duration) {
    if (!std::isfinite(t) || !std::isfinite(duration) || duration < 1 || t < .1f || t >= duration) return 0;
    if (t < .5f) return (t - .1f) / .4f;
    if (t < duration - .25f) return 1;
    return (duration - t) / .25f;
}

// Absolute source-audio position, deliberately obvious diagnostic deformation.
inline float BridgeWeight(float t) {
    if (!std::isfinite(t) || t < 0.15f || t >= 1.95f) return 0.0f;
    if (t < 0.55f) return 0.5f * (t - 0.15f) / 0.4f;
    if (t < 0.80f) return 0.5f;
    if (t < 1.15f) return 0.5f + 0.5f * (t - 0.80f) / 0.35f;
    if (t < 1.50f) return 1.0f;
    return 1.0f - (t - 1.50f) / 0.45f;
}
