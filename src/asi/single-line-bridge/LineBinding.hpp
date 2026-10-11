#pragma once
#include <string_view>

struct TestLine {
    const wchar_t* actor;
    int entry, stringRef, exportId;
    const wchar_t* cue;
    const wchar_t* pawn; // Empty for the conversation player.
    float duration;
};
inline constexpr TestLine TestLines[] = {
    {L"Shepard", 8, 65217, 170153, L"nor10_joker_N.VO_65217_M", L"", 2.215f},
    {L"Joker", 6, 102170, 206769, L"nor10_joker_N.VO_102170_M",
        L"BIOA_NOR10_01_DS1.TheWorld.PersistentLevel.BioPawn_0", 4.853f},
    {L"Kaidan", 4, 102169, 206768, L"nor10_joker_N.VO_102169_M",
        L"BIOA_NOR10_01_DS1.TheWorld.PersistentLevel.BioPawn_2", 3.309f}
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
    return MatchesLineIds(line,resRef,*line.pawn ? currentEntry : line.entry,text,exportId,cue);
}
struct TestRig { const wchar_t* mesh; const wchar_t* object; int maxLOD; };
inline constexpr TestRig TestRigs[] = {
    {L"BIOG_HMM_HED_PROMorph.Sheppard.HMM_HED_PROSheppard_MDL", L"MEFSingleLineBridge.ShepardBridgeSet", 1},
    {L"BIOG_HMM_HED_PROMorph.Joker.HMM_HED_PROJoker_MDL", L"MEFJokerBridge.JokerBridgeSet", 0},
    {L"BIOG_HMM_HED_PROMorph.Kaiden.HMM_HED_PROKaiden_MDL", L"MEFKaidanBridge.KaidanBridgeSet", 1},
    // Both NPC components serialize this base head. Runtime must resolve one
    // of these exact meshes; do not guess topology on a dynamic clone.
    {L"BIOG_HMM_HED_PROMorph.Average.HMM_HED_PROAverage_MDL", L"MEFAverageBridge.AverageBridgeSet", 1}
};
inline const TestRig* MatchRig(const TestLine& line, std::wstring_view mesh) {
    if (!*line.pawn) return mesh == TestRigs[0].mesh ? &TestRigs[0] : nullptr;
    const int specific = std::wstring_view(line.actor)==L"Joker" ? 1 : 2;
    if (mesh == TestRigs[specific].mesh) return &TestRigs[specific];
    return mesh == TestRigs[3].mesh ? &TestRigs[3] : nullptr;
}
