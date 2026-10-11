#pragma once
#include <array>
#include <algorithm>
#include "GeneratedFaceOwnership.hpp"
namespace FaceOwnership {
inline bool RequiredMap(const std::array<unsigned char,BoneCount>& skeleton,const unsigned char* required,unsigned count,std::array<unsigned char,BoneCount>& expected) {
    if(!required || !count || count>255)return false;
    for(size_t i=0;i<BoneCount;++i){
        auto raw=skeleton[i];
        expected[i]=(raw!=255 && std::find(required,required+count,raw)!=required+count)?raw:255;
        if(Owned[i] && expected[i]==255)return false;
    }
    return true;
}
// Scope lasts only for one verified native FaceFX bone-output call. Engine
// curves, audio, graph evaluation and protected outputs still run normally.
// The native loops explicitly skip entries with value 255. No bone atom writes.
struct Mask {
    std::array<unsigned char,BoneCount> before{};
    unsigned char* data=nullptr;
    bool suppress=true;
    bool Apply(unsigned char* map,unsigned count,const std::array<unsigned char,BoneCount>& expected,unsigned atomCount,bool suppressOutputs=true) {
        if(data || !map || count!=BoneCount || !std::equal(expected.begin(),expected.end(),map))return false;
        for(auto b:expected)if(b!=255 && b>=atomCount)return false;
        std::copy_n(map,BoneCount,before.begin());data=map;suppress=suppressOutputs;
        for(size_t i=0;i<BoneCount;++i)if(suppress && Owned[i])map[i]=255;
        return true;
    }
    bool Restore(unsigned char* map,unsigned count) {
        if(!data || data!=map || count!=BoneCount)return false;
        // A changed entry means an unexpected nested/engine writer; refuse to
        // overwrite its new values. Caller disables intervention and reports.
        for(size_t i=0;i<BoneCount;++i)if(map[i]!=(suppress && Owned[i]?255:before[i]))return false;
        std::copy(before.begin(),before.end(),map);data=nullptr;return true;
    }
};
// A normal native map rebuild may resize/free the array. Restore BEFORE that
// writer runs, then validate/reapply on its new array. Never restore an old
// snapshot into replacement storage or accept partially changed masked bytes.
struct Lifecycle {
    Mask mask;
    unsigned rebuilds=0;
    bool refused=false;
    bool BeforeRebuild(unsigned char* map,unsigned count) {
        ++rebuilds;
        if(!mask.data)return true; // already withheld: nothing to restore
        if(mask.Restore(map,count))return true;
        refused=true;return false;
    }
    bool AfterRebuild(unsigned char* map,unsigned count,const std::array<unsigned char,BoneCount>& expected,unsigned atoms,bool identityValid) {
        if(refused || !identityValid || !mask.Apply(map,count,expected,atoms,mask.suppress)){refused=true;return false;}
        return true;
    }
    bool Finish(unsigned char* map,unsigned count) {
        return !mask.data || mask.Restore(map,count);
    }
};
}
