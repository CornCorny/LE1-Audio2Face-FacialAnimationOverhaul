#include "ResourceTrace.hpp"
#include "RenderTraceNative.hpp"
#include "ResourceTraceNative.hpp"
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
void Require(bool value,const char* message) {if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main() {
    ResourceTrace::Gate gate;unsigned phase=0,ordinal=0;
    Require(!gate.Claim(phase,ordinal),"unarmed gates never collect");gate.Arm(1);
    std::vector<unsigned> samples;
    for(unsigned i=0;i<900;++i)if(gate.Claim(phase,ordinal)){Require(phase==1,"phase remains attached to claimed ordinal");samples.push_back(ordinal);}
    Require(samples==std::vector<unsigned>{0,1,2,3,15,63,127,511},"bounded initial and delayed samples");
    gate.Arm(2);Require(gate.Claim(phase,ordinal) && phase==2 && ordinal==0,"new transition rearms observation");
    ResourceTrace::Queue<std::uint64_t,2> small;
    Require(small.Push(11) && small.Push(22) && !small.Push(33),"full queue drops without waiting");
    Require(small.dropped.load()==1,"queue loss is explicit");
    std::vector<std::uint64_t> values;small.Drain([&](auto v){values.push_back(v);});
    Require(values==std::vector<std::uint64_t>{11,22} && small.Push(44),"ready records drained once and slots recycled");
    struct Payload {std::uint64_t id=0,inverse=0;std::array<std::uint64_t,32> words{};};
    ResourceTrace::Queue<Payload,16> queue;
    std::atomic<unsigned> finished{0},accepted{0};
    std::array<unsigned char,8000> seen{};unsigned consumed=0;
    std::vector<std::thread> producers;
    for(unsigned p=0;p<4;++p)producers.emplace_back([&,p]{
        for(unsigned i=0;i<2000;++i){Payload v;v.id=p*2000+i;v.inverse=~v.id;v.words.fill(v.id);if(queue.Push(v))accepted.fetch_add(1);}
        finished.fetch_add(1,std::memory_order_release);
    });
    auto consume=[&](const Payload& v){Require(v.id<seen.size() && !seen[v.id],"concurrent records unique and intact");Require(v.inverse==~v.id,"record publication not torn");for(auto word:v.words)Require(word==v.id,"full payload published before read");seen[v.id]=1;++consumed;};
    while(finished.load(std::memory_order_acquire)!=4){queue.Drain(consume);std::this_thread::yield();}
    for(auto& t:producers)t.join();queue.Drain(consume);
    Require(consumed==accepted.load() && consumed+queue.dropped.load()==8000,"every concurrent attempt accounted for");
    auto bytes=ResourceTraceNative::InstanceResourceBytes;
    Require(RenderTraceNative::Matches(bytes.data(),ResourceTraceNative::InstanceResourceBytes),"full resource function accepted");
    bytes[100]^=1;Require(!RenderTraceNative::Matches(bytes.data(),ResourceTraceNative::InstanceResourceBytes),"fallback body mutation refused");
    std::puts("PASS: resource sampling/phase bounds, nonblocking full queue, concurrent publication/integrity/accounting and native body guards. Native LE1 execution remains untested.");
}
