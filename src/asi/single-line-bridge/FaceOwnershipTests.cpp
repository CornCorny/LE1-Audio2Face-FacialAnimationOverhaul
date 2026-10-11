#include "FaceOwnership.hpp"
#include "FaceOwnershipNative.hpp"
#include "FaceRebuildNative.hpp"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::printf("Failure line %d\n",__LINE__);return 1;}}while(0)
int main(){
    using namespace FaceOwnership;
    CHECK(FaceOwnershipNative::Matches(FaceOwnershipNative::Bytes.data()));
    auto code=FaceOwnershipNative::Bytes;code.back()^=1;
    CHECK(!FaceOwnershipNative::Matches(code.data()));CHECK(!FaceOwnershipNative::Matches(nullptr));
    CHECK(FaceRebuildNative::Matches(FaceRebuildNative::Bytes.data()));
    auto rebuildCode=FaceRebuildNative::Bytes;rebuildCode.back()^=1;
    CHECK(!FaceRebuildNative::Matches(rebuildCode.data()));CHECK(!FaceRebuildNative::Matches(nullptr));
    std::array<unsigned char,120> required{};for(unsigned i=0;i<120;++i)required[i]=static_cast<unsigned char>(119-i);
    std::array<unsigned char,BoneCount> expected{};
    CHECK(RequiredMap(ShepardMap,required.data(),required.size(),expected));CHECK(expected==ShepardMap);
    CHECK(!RequiredMap(ShepardMap,nullptr,0,expected));CHECK(!RequiredMap(ShepardMap,required.data(),256,expected));
    unsigned protectedBone=0;while(Owned[protectedBone] || ShepardMap[protectedBone]==255)++protectedBone;
    auto reduced=required;for(auto& b:reduced)if(b==ShepardMap[protectedBone])b=255;
    CHECK(RequiredMap(ShepardMap,reduced.data(),reduced.size(),expected));CHECK(expected[protectedBone]==255);
    unsigned ownedBone=0;while(!Owned[ownedBone])++ownedBone;
    reduced=required;for(auto& b:reduced)if(b==ShepardMap[ownedBone])b=255;
    CHECK(!RequiredMap(ShepardMap,reduced.data(),reduced.size(),expected)); // withhold unsupported facial LOD
    for(auto expected:{ShepardMap,JokerMap,KaidanMap,AverageMap}){
        auto map=expected;Mask m;CHECK(m.Apply(map.data(),BoneCount,expected,120));
        for(size_t i=0;i<BoneCount;++i)CHECK(map[i]==(Owned[i]?255:expected[i]));
        CHECK(!m.Apply(map.data(),BoneCount,expected,120));
        CHECK(m.Restore(map.data(),BoneCount));CHECK(map==expected);
        CHECK(m.Apply(map.data(),BoneCount,expected,120));
        auto wrong=map;CHECK(!m.Restore(wrong.data(),BoneCount));CHECK(m.Restore(map.data(),BoneCount));
        map[0]=254;CHECK(!m.Apply(map.data(),BoneCount,expected,120));map=expected;
        CHECK(!m.Apply(map.data(),BoneCount-1,expected,120));
        CHECK(!m.Apply(map.data(),BoneCount,expected,1));
    }
    // Explicit protected reference entries: Head/HeadBase, eyelids, under-eyes,
    // eyeballs, tongue. Brows are intentionally owned, per user clarification.
    for(auto i:{8,9,12,15,16,31,36,37,38,39,40})CHECK(!Owned[i]);
    CHECK(Owned[10] && Owned[11] && Owned[13] && Owned[14]);
    auto map=ShepardMap;Mask m;CHECK(m.Apply(map.data(),BoneCount,ShepardMap,42));
    map[0]=254;CHECK(!m.Restore(map.data(),BoneCount));CHECK(map[0]==254);
    // Regression for Sync3: native dirty-map rebuild during a masked call.
    // The old array MUST be normal before the engine may resize/free it.
    for(bool replace:{false,true}){
        auto old=ShepardMap,newMap=AverageMap;Lifecycle life;
        CHECK(life.mask.Apply(old.data(),BoneCount,ShepardMap,120));
        CHECK(life.BeforeRebuild(old.data(),BoneCount));CHECK(old==ShepardMap);
        auto* live=replace?newMap.data():old.data();auto expected=replace?AverageMap:ShepardMap;
        CHECK(life.AfterRebuild(live,BoneCount,expected,120,true));
        CHECK(life.BeforeRebuild(live,BoneCount)); // a second writer in one call
        CHECK(std::equal(expected.begin(),expected.end(),live));
        CHECK(life.AfterRebuild(live,BoneCount,expected,120,true));
        CHECK(life.Finish(live,BoneCount));CHECK(life.rebuilds==2);
        CHECK(std::equal(expected.begin(),expected.end(),live));
        CHECK(life.mask.Apply(live,BoneCount,expected,120)); // next tick works
        CHECK(life.Finish(live,BoneCount));
    }
    auto old=ShepardMap;Lifecycle stopped;CHECK(stopped.mask.Apply(old.data(),BoneCount,ShepardMap,120));
    CHECK(stopped.BeforeRebuild(old.data(),BoneCount));
    CHECK(!stopped.AfterRebuild(nullptr,0,ShepardMap,0,false));
    CHECK(stopped.Finish(nullptr,0));CHECK(old==ShepardMap); // no stale write
    CHECK(stopped.BeforeRebuild(nullptr,0)); // later normal rebuild, still withheld
    CHECK(!stopped.AfterRebuild(old.data(),BoneCount,ShepardMap,120,true));
    CHECK(stopped.Finish(old.data(),BoneCount));CHECK(old==ShepardMap);
    Lifecycle corrupt;CHECK(corrupt.mask.Apply(old.data(),BoneCount,ShepardMap,120));
    old[0]=254;CHECK(!corrupt.BeforeRebuild(old.data(),BoneCount));
    CHECK(!corrupt.AfterRebuild(old.data(),BoneCount,ShepardMap,120,true));
    CHECK(!corrupt.Finish(old.data(),BoneCount));CHECK(old[0]==254);
    return 0;
}
