#include <cstddef>
#include "FaceTransition.hpp"
#include "FaceOwnership.hpp"
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>
struct Quat {float X=0,Y=0,Z=0,W=1;};
struct Vec {float X=0,Y=0,Z=0;};
struct Atom {Quat Rotation;Vec Translation;float Scale=1;};
#define CHECK(x) do{if(!(x)){std::printf("Failure line %d\n",__LINE__);return 1;}}while(0)
int main(){
 using namespace FaceTransition;
 CHECK(Envelope(0,2)==0 && Envelope(2,2)==0 && Envelope(.5f,2)==1);
 CHECK(Envelope(-1,2)==0 && Envelope(.5f,0)==0);
 CHECK(Envelope(std::numeric_limits<float>::quiet_NaN(),2)==0);
 CHECK(Envelope(.16f,2,.24f)<1 && Envelope(.24f,2,.24f)==1);
 CHECK(Envelope(.1f,2,0)==0 && Envelope(.1f,2,std::numeric_limits<float>::quiet_NaN())==0);
 CHECK(BrowChannel(L"MEF_A2F_browInnerUp") && BrowChannel(L"MEF_A2F_detailBrow1Positive"));
 CHECK(!BrowChannel(L"MEF_A2F_detailMouth1Positive") && !BrowChannel(L"MEF_A2F_jawOpen"));
 for(float fps:{30.f,60.f,120.f}){
  float last=0;for(int i=0;i<=int(fps*.16f);++i){float a=Envelope(i/fps,2);CHECK(a>=last&&a<=1);last=a;}
 }
 for(auto map:{FaceOwnership::ShepardMap,FaceOwnership::JokerMap,FaceOwnership::KaidanMap,FaceOwnership::AverageMap}){
  std::array<Atom,120> base{},vanilla{};
  for(unsigned i=0;i<120;++i){base[i].Translation.X=float(i);vanilla[i]=base[i];vanilla[i].Translation.X+=2;vanilla[i].Rotation={0,0,.6f,.8f};}
  std::array<Atom,FaceOwnership::BoneCount> before{};
  CHECK(Capture(base.data(),120,map,before));
  // Brow easing must remain transactional and leave every protected bone alone.
  for(auto pair:{std::pair{0.f,.5f},std::pair{1.f,.25f},std::pair{.5f,0.f}}){
   auto live=vanilla;CHECK(Blend(live.data(),120,map,before,pair.first,pair.second));
   for(unsigned b=0;b<120;++b){bool owned=false;float alpha=pair.first;
    for(unsigned i=0;i<map.size();++i)if(FaceOwnership::Owned[i]&&map[i]==b){owned=true;if(BrowBone(i))alpha=pair.second;}
    if(!owned)CHECK(std::memcmp(&live[b],&vanilla[b],sizeof(Atom))==0);
    else CHECK(std::abs(live[b].Translation.X-(float(b)+2*(1-alpha)))<.0001f);
   }
  }
  for(float alpha:{0.f,.01f,.5f,.99f,1.f}){
   auto live=vanilla;CHECK(Blend(live.data(),120,map,before,alpha));
   for(unsigned b=0;b<120;++b){bool owned=false;for(unsigned i=0;i<map.size();++i)if(FaceOwnership::Owned[i]&&map[i]==b)owned=true;
    if(!owned||alpha==0)CHECK(std::memcmp(&live[b],&vanilla[b],sizeof(Atom))==0);
    else {CHECK(std::abs(live[b].Translation.X-(float(b)+2*(1-alpha)))<.0001f);CHECK(Valid(live[b]));}
    if(owned&&alpha==1)CHECK(std::memcmp(&live[b],&base[b],sizeof(Atom))==0);
   }
  }
  // Equivalent quaternion signs must not collapse at half blend.
  auto live=base;for(auto& a:live)a.Rotation.W=-1;
  CHECK(Blend(live.data(),120,map,before,.5f));
  auto saved=vanilla;auto invalid=before;
  unsigned last=0;for(unsigned i=0;i<map.size();++i)if(FaceOwnership::Owned[i])last=i;
  invalid[last].Rotation.W=std::numeric_limits<float>::quiet_NaN();
  CHECK(!Blend(saved.data(),120,map,invalid,.5f));CHECK(std::memcmp(saved.data(),vanilla.data(),sizeof(saved))==0);
  CHECK(!Blend(saved.data(),1,map,before,.5f));CHECK(std::memcmp(saved.data(),vanilla.data(),sizeof(saved))==0);
  CHECK(!Blend(saved.data(),120,map,before,-.1f));
  CHECK(!Blend(saved.data(),120,map,before,.5f,std::numeric_limits<float>::quiet_NaN()));
  CHECK(std::memcmp(saved.data(),vanilla.data(),sizeof(saved))==0);
  auto dup=map;dup[11]=dup[10];CHECK(!Capture(base.data(),120,dup,before));
  auto m=map;FaceOwnership::Lifecycle life;
  CHECK(life.mask.Apply(m.data(),m.size(),map,120,false));CHECK(m==map);
  CHECK(life.BeforeRebuild(m.data(),m.size()));
  CHECK(life.AfterRebuild(m.data(),m.size(),map,120,true));CHECK(m==map);
  CHECK(life.Finish(m.data(),m.size()));CHECK(m==map);
 }
 return 0;
}
