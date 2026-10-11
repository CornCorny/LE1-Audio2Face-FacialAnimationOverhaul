#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

// No engine objects are accessed by the queue or sampling policy. Publication
// and consumption use acquire/release; render producers never wait for logging.
namespace ResourceTrace {
inline bool Sample(unsigned ordinal) {
    return ordinal<4 || ordinal==15 || ordinal==63 || ordinal==127 || ordinal==511;
}
struct Gate {
    std::atomic<std::uint64_t> state{0};
    void Arm(unsigned phase) {state.store(std::uint64_t(phase)<<32,std::memory_order_release);}
    bool Claim(unsigned& phase,unsigned& ordinal) {
        auto value=state.load(std::memory_order_acquire);
        // One attempt: contention drops an observation, never blocks the game.
        phase=static_cast<unsigned>(value>>32);ordinal=static_cast<unsigned>(value);
        if(!phase || ordinal>=512)return false;
        return state.compare_exchange_strong(value,value+1,std::memory_order_acq_rel) && Sample(ordinal);
    }
};
struct Event {
    std::uintptr_t proxy=0,vtable=0,owner=0,parent=0,candidate=0,candidate74=0,result=0,result74=0,caller=0;
    std::uint32_t flags=0;
    int quality=-1;
    unsigned depth=0,kind=0,reads=0;
};
struct Record {
    unsigned actor=0,phase=0,ordinal=0,thread=0,count=0;
    std::uint64_t milliseconds=0;
    bool overflow=false;
    std::array<Event,32> events{};
};
template<class T,std::size_t N> struct Queue {
    struct Slot {std::atomic<unsigned> state{0};T value{};};
    std::array<Slot,N> slots{};
    std::atomic<unsigned> dropped{0};
    bool Push(const T& record) {
        for(auto& slot:slots) {
            unsigned free=0;
            if(!slot.state.compare_exchange_strong(free,1,std::memory_order_acquire))continue;
            slot.value=record;slot.state.store(2,std::memory_order_release);return true;
        }
        dropped.fetch_add(1,std::memory_order_relaxed);return false;
    }
    template<class Consume> void Drain(Consume consume) {
        for(auto& slot:slots) {
            unsigned ready=2;
            if(!slot.state.compare_exchange_strong(ready,3,std::memory_order_acquire))continue;
            consume(slot.value);slot.state.store(0,std::memory_order_release);
        }
    }
};
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
}
