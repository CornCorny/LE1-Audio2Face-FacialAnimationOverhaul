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
        Check(!MatchRig(line,L"BIOG_TUR_HED.Nihlus"), "unrelated head refused");
    }
    Check(!MatchRig(TestLines[0],TestRigs[3].mesh), "shared NPC base never used for custom Shepard");
    Check(MatchRig(TestLines[1],TestRigs[1].mesh) && MatchRig(TestLines[2],TestRigs[2].mesh), "specific NPC heads");
    Check(MatchRig(TestLines[1],TestRigs[3].mesh) && MatchRig(TestLines[2],TestRigs[3].mesh), "serialized NPC base heads");
    Check(!MatchRig(TestLines[1],TestRigs[2].mesh) && !MatchRig(TestLines[2],TestRigs[1].mesh), "cross-character rig rejected");
    std::puts("PASS: ten Try1 samples accept corrected identity; unrelated lines/voices/heads rejected. Live engine binding remains untested.");
}
