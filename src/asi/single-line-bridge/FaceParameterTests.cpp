#include "FaceParameters.hpp"
#include <cstdio>
#include <cstdlib>
struct Parameter {int key,value;};
struct Array:std::vector<Parameter>{using std::vector<Parameter>::vector;unsigned Count()const{return static_cast<unsigned>(size());}};
void Check(bool b,const char* text){if(!b){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main(){
    Array leaf={{1,100}},middle={{1,50},{2,200}},root={{2,20},{3,300}};
    std::vector<const Array*> layers={&leaf,&middle,&root};
    auto matches=[&](const Array& actual){return FaceParameters::Matches<Parameter>(layers,actual,[](auto& a){return a.key;},[](auto& a,auto& b){return a.value==b.value;});};
    Check(matches(Array{{1,100},{2,200},{3,300}}),"inherited texture/scalar values and leaf overrides preserved");
    Check(matches(Array{{3,300},{2,200},{1,100}}),"parameter order independent");
    Check(!matches(Array{{1,100}}),"leaf-only copy loses inherited appearance");
    Check(!matches(Array{{1,50},{2,200},{3,300}}),"parent may not overwrite leaf");
    Check(!matches(Array{{1,100},{2,20},{3,300}}),"nearest ancestor wins");
    Check(!matches(Array{{1,100},{2,200},{3,300},{4,400}}),"unexpected inherited value rejected");
    std::puts("PASS: layered material parameter preservation and override precedence.");
}
