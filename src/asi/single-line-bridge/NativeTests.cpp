#include "NativeMorph.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
struct Entry { int* Target; float Weight; };
struct List {
    std::vector<Entry> entries;
    unsigned Count() const { return static_cast<unsigned>(entries.size()); }
    Entry& operator()(unsigned i) { return entries.at(i); }
    void Erase(unsigned i) { entries.erase(entries.begin()+i); }
};
void Check(bool good, const char* message) {
    if (!good) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main() {
    auto pose=NativeMorph::PoseBytes; auto eval=NativeMorph::EvaluateBytes;
    Check(NativeMorph::Matches(pose.data(),eval.data()),"known code accepted");
    pose[0]^=1;
    Check(!NativeMorph::Matches(pose.data(),eval.data()),"changed pose code refused");
    pose=NativeMorph::PoseBytes; eval[23]^=1;
    Check(!NativeMorph::Matches(pose.data(),eval.data()),"changed evaluator refused");
    Check(!NativeMorph::Matches(nullptr,eval.data()),"null code refused");
    int ours=0, vanillaA=0, vanillaB=0;
    List list{{{&vanillaA,.37f},{&ours,1},{&vanillaB,.62f},{&ours,.5f}}};
    NativeMorph::RemoveOwned(list,&ours);
    Check(list.Count()==2 && list(0).Target==&vanillaA && list(0).Weight==.37f &&
        list(1).Target==&vanillaB && list(1).Weight==.62f,"cleanup preserves unrelated targets/weights/order");
    for (int i=0;i<100;++i) {
        NativeMorph::RemoveOwned(list,&ours);
        list.entries.push_back({&ours,.8f}); // Model the native append contract.
        Check(list.Count()==3,"repeated pose callback does not accumulate owned entries");
    }
    NativeMorph::RemoveOwned(list,&ours);
    NativeMorph::RemoveOwned(list,&ours);
    Check(list.Count()==2,"repeated release is idempotent");
    std::array<int,38> channels{};
    for(unsigned pass=0;pass<4;++pass){
        for(unsigned i=0;i<channels.size();++i){list.entries.push_back({&channels[i],.2f});list.entries.push_back({&channels[i],.7f});}
        for(auto& channel:channels)NativeMorph::RemoveOwned(list,&channel);
        Check(list.Count()==2&&list(0).Target==&vanillaA&&list(1).Target==&vanillaB,"38-channel duplicate cleanup preserves vanilla order");
    }
    std::puts("PASS: native code guards; owned-target cleanup preserves vanilla entries and handles repeated pose updates. Native execution/rendering is not tested here.");
}
