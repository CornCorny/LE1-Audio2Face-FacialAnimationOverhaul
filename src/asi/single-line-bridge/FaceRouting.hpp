#pragma once
#include <array>
#include <cstdint>
namespace FaceRouting {
enum class Source : unsigned { Head, Parent };
// Coverage belongs to a component evaluation in THIS engine tick, not to a
// nested head-pose scope. The parent may evaluate before or around its child.
struct Coverage {
    struct State {std::uint64_t seen=0,masked=0;};
    std::array<State,2> state{};
    void Begin(Source source,std::uint64_t frame){state[static_cast<unsigned>(source)]={frame,0};}
    void Applied(Source source,std::uint64_t frame){state[static_cast<unsigned>(source)]={frame,frame};}
    void Refused(Source source,std::uint64_t frame){state[static_cast<unsigned>(source)]={frame,0};}
    bool Isolated(std::uint64_t frame)const {
        if(!frame)return false;
        bool applied=false;
        for(auto s:state){
            if(s.seen==frame && s.masked!=frame)return false;
            applied|=s.masked==frame;
        }
        return applied;
    }
};
}
