#include "FacialClip.hpp"
#include "LineBinding.hpp"
#include "GeneratedScene.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <array>
void Check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}}
template<class T> void Put(std::vector<unsigned char>& b,size_t offset,T v){std::memcpy(b.data()+offset,&v,sizeof(v));}
int main(int argc,char** argv){
    Check(argc==2,"clip directory");
    FacialClip::Clip c;std::vector<unsigned char>b(32+3*8);
    Put(b,0,0x4346454du);Put(b,4,1u);Put(b,8,1u);Put(b,12,3u);Put(b,16,16000u);Put(b,20,16000u);Put(b,24,std::uint64_t{123});
    Put(b,32,0u);Put(b,36,0.f);Put(b,40,8000u);Put(b,44,.8f);Put(b,48,16000u);Put(b,52,0.f);
    Check(c.Decode(b,123,1,FacialClip::Hash(b)),"valid bounded clip");
    float value=0;
    for(unsigned fps:{24u,30u,60u,120u})for(unsigned i=0;i<=fps;++i){float t=static_cast<float>(i)/fps;Check(c.Sample(t,&value,1),"sample frame-rate variation");Check(std::abs(value-1.6f*std::min(t,1-t))<.00001f,"linear audio-time interpolation");}
    Check(c.Sample(.7f,&value,1)&&c.Sample(.2f,&value,1)&&std::abs(value-.32f)<.00001f,"backward seek independent of prior frame");
    Check(c.Sample(1.2f,&value,1)&&value==0,"post-audio neutral");
    Check(!c.Sample(-1,&value,1)&&!c.Sample(std::numeric_limits<float>::quiet_NaN(),&value,1),"invalid clocks refused");
    Check(!c.Decode(b,124,1,FacialClip::Hash(b)),"schema mismatch");
    Check(!c.Decode(b,123,1,0),"file hash mismatch");
    auto bad=b;Put(bad,44,std::numeric_limits<float>::quiet_NaN());Check(!c.Decode(bad,123,1,FacialClip::Hash(bad)),"nonfinite channel refused");
    bad=b;Put(bad,40,0u);Check(!c.Decode(bad,123,1,FacialClip::Hash(bad)),"nonmonotonic timestamps refused");
    bad=b;bad.pop_back();Check(!c.Decode(bad,123,1,FacialClip::Hash(bad)),"truncated payload refused");
    bad=b;Put(bad,12,0xffffffffu);Check(!c.Decode(bad,123,1,FacialClip::Hash(bad)),"oversized frame count refused");
    bad=b;Put(bad,52,.1f);Check(!c.Decode(bad,123,1,FacialClip::Hash(bad)),"nonneutral final frame refused");
    unsigned shepard=0,joker=0,kaidan=0;
    for(size_t i=0;i<std::size(SceneLines);++i){
        auto const& line=SceneLines[i];
        Check(MatchesSceneLineIds(line,205,line.entry,line.stringRef,line.exportId,line.cue),"scene identity");
        if(!*line.participantTag){
            const int parentEntry=line.entry<20 ? 8 : 18;
            Check(MatchesSceneLineIds(line,205,parentEntry,line.stringRef,line.exportId,line.cue),"all Shepard voices accepted with parent NPC entry index");
            Check(!MatchesSceneLineIds(line,205,parentEntry,line.stringRef,line.exportId+1,line.cue),"wrong live reply metadata refused");
        }else Check(!MatchesSceneLineIds(line,205,line.entry+1,line.stringRef,line.exportId,line.cue),"wrong NPC entry refused");
        Check(!MatchesLineIds(line,205,line.entry,line.stringRef,line.exportId,L"nor10_joker_N.VO_65217_F"),"female voice excluded");
        if(!*line.participantTag){++shepard;Check(TestRigs[0].playerOnly&&!TestRigs[3].playerOnly,"default male coverage profile");}
        else if(std::wstring_view(line.actor)==L"Joker"){++joker;Check(line.entry!=0&&line.entry!=24,"re-entry scenes excluded");}
        else ++kaidan;
        auto path=std::filesystem::path(argv[1])/(L"VO_"+std::to_wstring(line.stringRef)+L"_M.mefc");
        Check(c.Load(path,FacialSchemaHash,std::size(FacialChannels),SceneClipHashes[i]),"generated clip round trip/hash");
        Check(std::abs(static_cast<float>(c.audioSamples)/c.sampleRate-line.duration)<.00001f,"manifest duration matches audio");
        std::array<float,std::size(FacialChannels)> frame{};for(float t:{0.f,line.duration*.25f,line.duration*.5f,line.duration*.75f,line.duration})Check(c.Sample(t,frame.data(),frame.size()),"generated samples");
    }
    Check(shepard==6&&joker==19&&kaidan==5,"all opening spoken lines and variants covered");
    bool jumpsuit=false;for(auto const& line:SceneLines)if(line.stringRef==101470)jumpsuit=line.entry==5&&std::wstring_view(line.actor)==L"Joker";
    Check(jumpsuit,"explicit hench_pilot speaker-list line included");
    std::puts("PASS: 30 GPU-authored clips, all Shepard alternatives, explicit Joker slot, protected scope, interpolation/seek and corrupt-file rejection.");
}
