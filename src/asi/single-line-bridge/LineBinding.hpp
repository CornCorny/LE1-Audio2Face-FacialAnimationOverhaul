#pragma once
#include <string_view>

struct TestLine {
    const wchar_t* actor;
    int entry, stringRef, exportId;
    const wchar_t* cue;
    const wchar_t* participantTag; // Conversation role; empty for the player.
    float duration;
};
inline constexpr TestLine TestLines[] = {
    {L"Shepard", 8, 65217, 170153, L"nor10_joker_N.VO_65217_M", L"", 2.215f},
    {L"Joker", 6, 102170, 206769, L"nor10_joker_N.VO_102170_M",
        L"hench_pilot", 4.853f},
    {L"Kaidan", 4, 102169, 206768, L"nor10_joker_N.VO_102169_M",
        L"hench_humanmale", 3.309f}
};
// Transient m_nSelectedReply is NOT a package reply-list index. Try1 recorded
// slot 0 while the exact voice for package reply 8 was playing.
inline bool MatchesLineIds(const TestLine& line, int resRef, int entry,
    int text, int exportId, std::wstring_view cue) {
    return resRef == 205 && entry == line.entry && text == line.stringRef &&
        exportId == line.exportId && cue == line.cue;
}
// m_nCurrentEntry is also used by ConvoSniffer to index the NPC entry's reply
// choices; it must not be assumed to equal every selected reply-list index.
// For player speech, exact live reply metadata + unique current voice cue +
// the runtime player-speaker/head/audio gates establish identity. The selected
// reply UI slot remains deliberately unused. NPC speech keeps its entry gate.
inline bool MatchesSceneLineIds(const TestLine& line,int resRef,int currentEntry,
    int text,int exportId,std::wstring_view cue){
    return MatchesLineIds(line,resRef,*line.participantTag ? currentEntry : line.entry,text,exportId,cue);
}
// Catalog of authored mesh assets, not character locations. Runtime resolution
// uses the loaded MorphTargetSet.BaseSkelMesh pointer plus structure checks.
struct TestRig { const wchar_t* object; int maxLOD; unsigned bones, lods; bool playerOnly; };
#include "GeneratedHeadRigs.hpp"
inline bool CompatibleRig(const TestRig& rig, bool player, unsigned bones, unsigned lods,
    const void* actualMesh, const void* baseMesh, unsigned channels, unsigned expectedChannels) {
    return rig.playerOnly==player && rig.bones==bones && rig.lods==lods &&
        actualMesh && actualMesh==baseMesh && channels==expectedChannels;
}
inline bool CompatibleParticipant(std::wstring_view expected, std::wstring_view actual,
    bool player, bool rolePresent, bool resolvedParticipantMatches) {
    return !expected.empty() && expected==actual && !player && rolePresent && resolvedParticipantMatches;
}
