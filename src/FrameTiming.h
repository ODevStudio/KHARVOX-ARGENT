#pragma once
#include "Diagnostics.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <mutex>
namespace argent {
void log(const std::string&);
// Coarse frame boundaries only: never used in per-draw hooks.
struct FrameTiming {
    using Clock=std::chrono::steady_clock;
    Clock::time_point start{};
    const char* section;
    struct Totals {std::mutex mutex;uint64_t count{};double sum{},max{};Clock::time_point lastReport{};};
    Totals& totals;
    bool enabled;
    FrameTiming(const char* name,Totals& t):section(name),totals(t),enabled(extendedLogging()){if(enabled)start=Clock::now();}
    ~FrameTiming() noexcept {
        if(!enabled)return;
        try{
            const auto now=Clock::now();
            const double ms=std::chrono::duration<double,std::milli>(now-start).count();
            std::lock_guard<std::mutex> guard(totals.mutex);
            totals.sum+=ms;if(ms>totals.max)totals.max=ms;
            if(++totals.count>=120&&now-totals.lastReport>=std::chrono::seconds(1)){
                const auto count=totals.count;const auto mean=totals.sum/count;const auto maximum=totals.max;
                totals.lastReport=now;totals.count=0;totals.sum=totals.max=0;
                log(std::string("FRAME_CPU section=")+section+" samples="+std::to_string(count)+" meanMs="+std::to_string(mean)+" maxMs="+std::to_string(maximum));
            }
        }catch(...){}
    }
};
}
