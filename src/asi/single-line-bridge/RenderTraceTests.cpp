#include "RenderTrace.hpp"
#include "RenderTraceNative.hpp"
#include <vector>
#include <cstdio>
#include <cstdlib>
void Check(bool good,const char* message){if(!good){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main() {
    using namespace RenderTrace;
    std::vector<unsigned char> memory(8192);
    auto Write=[&](size_t at,const auto& value){std::memcpy(memory.data()+at,&value,sizeof(value));};
    auto Read=[&](std::uintptr_t at,void* out,size_t bytes){if(at>memory.size() || bytes>memory.size()-at)return false;std::memcpy(out,memory.data()+at,bytes);return true;};
    Write(16,Array{128,2,2});Write(128,Array{256,2,2});Write(144,Array{512,1,1});
    const std::uint64_t first=0x1122334455667788,second=0x8877665544332211,third=0x1234;
    Write(256,first);Write(264,std::uint32_t{7});Write(268,second);Write(276,std::uint32_t{9});Write(512,third);Write(520,std::uint32_t{11});
    Snapshot s;Capture(16,s,Read);
    Check(!s.invalid && !s.truncated && s.count==3,"nested arrays captured");
    Check(s.entries[0].pointer==first && s.entries[0].flags==7 && s.entries[0].lod==0 && s.entries[0].slot==0,"first material and flag");
    Check(s.entries[1].pointer==second && s.entries[1].flags==9 && s.entries[1].slot==1,"12-byte stride, not padded 16-byte stride");
    Check(s.entries[2].pointer==third && s.entries[2].lod==1 && s.entries[2].slot==0,"second LOD identity");
    auto original=memory;s={};Capture(16,s,Read);Check(original==memory,"capture never writes source memory");
    Write(16,Array{128,-1,2});s={};Capture(16,s,Read);Check(s.invalid,"negative count refused");
    Write(16,Array{128,2,1});s={};Capture(16,s,Read);Check(s.invalid,"count beyond capacity refused");
    Write(16,Array{0,1,1});s={};Capture(16,s,Read);Check(s.invalid,"null populated array refused");
    Write(16,Array{128,9,9});s={};Capture(16,s,Read);Check(s.invalid,"LOD limit enforced");
    Write(16,Array{8190,1,1});s={};Capture(16,s,Read);Check(s.invalid,"unreadable array safely refused");
    Write(16,Array{128,1,1});Write(128,Array{256,65,65});s={};Capture(16,s,Read);Check(s.invalid,"slot limit enforced");
    Write(16,Array{});s={};Capture(16,s,Read);Check(!s.invalid && s.count==0,"empty array accepted");
    Write(16,Array{128,3,3});for(unsigned i=0;i<3;i++)Write(128+16*i,Array{1024,64,64});s={};Capture(16,s,Read);Check(s.truncated && s.count==128,"fixed total capture limit enforced");
    std::uintptr_t at=0;Check(!At(UINTPTR_MAX-4,1,12,at),"address addition overflow refused");
    Check(CopySite(0x5b4afd) && CopySite(0x5b4c39) && CopySite(0x5b4c78) && CopySite(0x5b4cb4) && CopySite(0x5b4cc5) && !CopySite(0x5b4afa),"only disassembled call-return sites accepted");
    Budget b;Check(b.Begin(false) && !b.Begin(false) && b.Begin(true) && !b.Begin(true) && b.Begin(false),"capture transitions only");
    for(int i=0;i<20;i++)b.Begin(i%2==0);Check(b.captures==6,"per-NPC capture budget enforced");
    CaptureData data;for(unsigned i=0;i<300;i++)data.Add({});Check(data.checkCount==256 && data.overflow,"usage buffer bounded");
    auto code=RenderTraceNative::InstanceUsageBytes;
    Check(RenderTraceNative::Matches(code.data(),RenderTraceNative::InstanceUsageBytes),"verified native bytes accepted");
    code[12]^=1;Check(!RenderTraceNative::Matches(code.data(),RenderTraceNative::InstanceUsageBytes),"changed native code refused");
    Check(!RenderTraceNative::Matches(nullptr,RenderTraceNative::InstanceUsageBytes),"null code refused");
    std::puts("PASS: read-only packed proxy-array capture, bounds/read failures/overflow, callsite filtering, transition budgets and native-code guards. Actual LE1 calls remain untested.");
}
