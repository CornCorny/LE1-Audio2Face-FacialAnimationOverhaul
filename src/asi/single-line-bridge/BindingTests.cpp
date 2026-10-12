#include "LineBinding.hpp"
#include <fstream>
#include <string>
#include <regex>
#include <cstdio>
#include <cstdlib>
void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
int main(int argc, char** argv) {
    Check(argc == 2, "Try1 evidence path supplied");
    std::ifstream in(argv[1]);
    Check(in.is_open(), "recorded Try1 log opened");
    std::string row; unsigned observed = 0;
    std::regex fields(R"(entry=(\d+) reply=(\d+).*cue=nor10_joker_N.VO_65217_M )");
    while (std::getline(in, row)) {
        std::smatch match;
        if (row.find("OBSERVE") == std::string::npos || !std::regex_search(row, match, fields)) continue;
        ++observed;
        int entry = std::stoi(match[1]), selectedSlot = std::stoi(match[2]);
        Check(selectedSlot != 8, "recorded transient reply differs from package index");
        Check(MatchesLineIds(TestLines[0],205,entry,65217,170153,L"nor10_joker_N.VO_65217_M"),
            "corrected identity accepts recorded target despite transient slot");
    }
    Check(observed == 10, "all ten recorded target-cue samples covered");
    for (auto const& line : TestLines) {
        Check(MatchesLineIds(line,205,line.entry,line.stringRef,line.exportId,line.cue), "configured identity");
        Check(!MatchesLineIds(line,206,line.entry,line.stringRef,line.exportId,line.cue), "other conversation rejected");
        Check(!MatchesLineIds(line,205,line.entry+1,line.stringRef,line.exportId,line.cue), "other entry rejected");
        Check(!MatchesLineIds(line,205,line.entry,line.stringRef,line.exportId+1,line.cue), "other export rejected");
        Check(!MatchesLineIds(line,205,line.entry,line.stringRef,line.exportId,L"nor10_joker_N.VO_65217_F"), "other voice rejected");
    }
    int actual=0,other=0;
    Check(CompatibleParticipant(L"hench_pilot",L"hench_pilot",false,true,true),"participant binding needs no pawn package path");
    Check(!CompatibleParticipant(L"hench_pilot",L"nor10_nihlus",false,true,true),"wrong participant/species role rejected");
    Check(!CompatibleParticipant(L"hench_pilot",L"hench_pilot",true,true,true),"player cannot impersonate NPC line");
    Check(!CompatibleParticipant(L"hench_pilot",L"hench_pilot",false,false,true),"nonparticipant actor rejected");
    Check(!CompatibleParticipant(L"hench_pilot",L"hench_pilot",false,true,false),"resolved pointer disagreement rejected");
    for(const auto& rig:TestRigs) {
        Check(CompatibleRig(rig,rig.playerOnly,rig.bones,rig.lods,&actual,&actual,48,48),"authored base mesh relationship");
        Check(!CompatibleRig(rig,rig.playerOnly,rig.bones,rig.lods,&actual,&other,48,48),"same structure with different vertex asset refused");
        Check(!CompatibleRig(rig,rig.playerOnly,rig.bones,rig.lods,&actual,nullptr,48,48),"unresolved base refused");
        Check(!CompatibleRig(rig,rig.playerOnly,rig.bones+1,rig.lods,&actual,&actual,48,48),"changed skeleton refused");
        Check(!CompatibleRig(rig,rig.playerOnly,rig.bones,rig.lods+1,&actual,&actual,48,48),"changed LOD layout refused");
        Check(!CompatibleRig(rig,rig.playerOnly,rig.bones,rig.lods,&actual,&actual,38,48),"old channel schema refused");
        Check(!CompatibleRig(rig,!rig.playerOnly,rig.bones,rig.lods,&actual,&actual,48,48),"player coverage remains default male only");
    }
    std::puts("PASS: ten Try1 samples accept corrected identity; unrelated lines/voices/heads rejected. Live engine binding remains untested.");
}
