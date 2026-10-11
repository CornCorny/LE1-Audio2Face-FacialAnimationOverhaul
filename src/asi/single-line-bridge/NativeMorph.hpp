#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// These addresses/signatures are verified against the user's supported LE1
// executable AND its loaded code. See research/single-line/try2/native/.
namespace NativeMorph {
inline constexpr std::uintptr_t PoseRva = 0x592440;
inline constexpr std::uintptr_t EvaluateRva = 0x438d40;
inline constexpr std::array<unsigned char,24> PoseBytes = {
    0x48,0x8b,0xc4,0x44,0x89,0x40,0x18,0x55,0x56,0x57,0x41,0x54,
    0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x6c,0x24,0xb0,0x48};
inline constexpr std::array<unsigned char,24> EvaluateBytes = {
    0x40,0x53,0x56,0x57,0x48,0x83,0xec,0x30,0x33,0xdb,0x48,0x8b,
    0xf2,0x48,0x8b,0xf9,0x39,0x59,0x7c,0x0f,0x8e,0xbd,0x00,0x00};
inline bool Matches(const void* pose, const void* evaluate) {
    return pose && evaluate && !std::memcmp(pose,PoseBytes.data(),PoseBytes.size()) &&
        !std::memcmp(evaluate,EvaluateBytes.data(),EvaluateBytes.size());
}
// Never clear the complete active list. Remove only our exact target pointer,
// including duplicates from repeated pose updates. The engine appends its own
// evaluated FActiveMorph; this helper preserves every unrelated entry/order.
template<class Array, class Target> void RemoveOwned(Array& array, Target target) {
    for (unsigned i=array.Count(); i>0; --i)
        if (array(i-1).Target == target) array.Erase(i-1);
}
}
