#include "FaceRouting.hpp"
#include <cstdio>
#define CHECK(x) do {if(!(x)){std::printf("Failure line %d\n",__LINE__);return 1;}}while(0)
int main(){
    using namespace FaceRouting;Coverage c;
    CHECK(!c.Isolated(0));CHECK(!c.Isolated(1));
    // Regression: parent evaluates and restores while no head-pose scope is
    // open. Child bypasses FaceFX. This MUST establish current-tick coverage.
    c.Begin(Source::Parent,1);c.Applied(Source::Parent,1);CHECK(c.Isolated(1));
    CHECK(!c.Isolated(2)); // never reuse previous-frame coverage
    c.Begin(Source::Parent,2);CHECK(!c.Isolated(2));
    c.Applied(Source::Parent,2);CHECK(c.Isolated(2));
    c.Refused(Source::Head,2);CHECK(!c.Isolated(2)); // no mixed failed source
    c.Applied(Source::Head,2);CHECK(c.Isolated(2));
    c.Refused(Source::Parent,2);CHECK(!c.Isolated(2));
    c={};c.Applied(Source::Head,3);CHECK(c.Isolated(3));
    c={};CHECK(!c.Isolated(3)); // release clears all evidence
    return 0;
}
