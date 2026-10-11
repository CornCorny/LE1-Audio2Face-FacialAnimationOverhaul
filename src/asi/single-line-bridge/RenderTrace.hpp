#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>

// LE1's copied proxy layout is TArray<TArray<12-byte material records>>.
// These are non-owning snapshots, not SDK objects or mutable engine arrays.
namespace RenderTrace {
struct Array {std::uintptr_t data=0;std::int32_t count=0,capacity=0;};
static_assert(sizeof(Array)==16 && offsetof(Array,count)==8);
struct Material {std::uintptr_t pointer=0;std::uint32_t flags=0;unsigned lod=0,slot=0;};
struct Snapshot {
    std::uintptr_t caller=0;
    std::array<Material,128> entries{};
    unsigned count=0;
    bool invalid=false,truncated=false;
};
inline bool Valid(const Array& a,unsigned limit) {
    return a.count>=0 && a.capacity>=a.count && static_cast<unsigned>(a.count)<=limit && (a.count==0 || a.data!=0);
}
inline bool At(std::uintptr_t base,unsigned index,unsigned stride,std::uintptr_t& result) {
    auto offset=static_cast<std::uintptr_t>(index)*stride;
    if(base>std::numeric_limits<std::uintptr_t>::max()-offset)return false;
    result=base+offset;return true;
}
template<class Read> void Capture(std::uintptr_t address,Snapshot& out,Read read) {
    Array lods;
    if(!read(address,&lods,sizeof(lods)) || !Valid(lods,8)){out.invalid=true;return;}
    for(unsigned lod=0;lod<static_cast<unsigned>(lods.count);++lod) {
        std::uintptr_t pos=0;Array slots;
        if(!At(lods.data,lod,16,pos) || !read(pos,&slots,sizeof(slots)) || !Valid(slots,64)){out.invalid=true;return;}
        for(unsigned slot=0;slot<static_cast<unsigned>(slots.count);++slot) {
            if(out.count>=out.entries.size()){out.truncated=true;return;}
            std::array<unsigned char,12> raw{};
            if(!At(slots.data,slot,12,pos) || !read(pos,raw.data(),raw.size())){out.invalid=true;return;}
            auto& e=out.entries[out.count++];
            std::memcpy(&e.pointer,raw.data(),8);std::memcpy(&e.flags,raw.data()+8,4);e.lod=lod;e.slot=slot;
        }
    }
}
inline bool CopySite(std::uintptr_t rva) {
    return rva==0x5b4afd || rva==0x5b4c39 || rva==0x5b4c78 || rva==0x5b4cb4 || rva==0x5b4cc5;
}
inline const wchar_t* CopyStage(std::uintptr_t rva) {
    return rva==0x5b4afd ? L"input" : rva==0x5b4cc5 ? L"sync-proxy-update" : L"selected-for-render-command";
}
struct Usage {
    std::uintptr_t object=0,instanceObject=0,caller=0;
    int usage=0,result=0,recompile=0;
    bool base=false;
};
struct CaptureData {
    std::array<Snapshot,4> copies{};
    std::array<Usage,256> checks{};
    unsigned copyCount=0,checkCount=0;
    std::uintptr_t currentInterface=0;
    bool overflow=false;
    void Add(Usage event) {
        if(checkCount<checks.size())checks[checkCount++]=event;else overflow=true;
    }
};
struct Budget {
    unsigned captures=0;
    bool initialized=false,previous=false;
    bool Begin(bool active) {
        bool changed=!initialized || active!=previous;
        initialized=true;previous=active;
        if(!changed || captures>=6)return false;
        ++captures;return true;
    }
};
}
