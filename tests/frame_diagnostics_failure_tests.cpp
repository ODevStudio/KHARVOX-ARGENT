#include "../src/FrameTiming.h"
#include "../src/PresentAnalysis.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool measureAllocations{};
thread_local unsigned allocationCountdown{};
unsigned allocationCalls{},allocationFailures{},logs{};
bool throwLog{},malformed{};
}
void* operator new(std::size_t size){
    if(measureAllocations)++allocationCalls;
    if(allocationCountdown&&!--allocationCountdown){++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value) noexcept {std::free(value);}
void operator delete(void* value,std::size_t) noexcept {std::free(value);}
namespace argent {
void log(const std::string& value){
    ++logs;if(throwLog)throw std::bad_alloc{};
    malformed|=value.empty();
    if(value.rfind("FRAME_TIMELINE ",0)==0){
        malformed|=value.find(" entriesUs=")==std::string::npos;
        if(perf::enabled())malformed|=value.find(" perfModes=")==std::string::npos||value.find(" fovStates=")==std::string::npos;
    }
    if(value.rfind("PROCESS_CPU ",0)==0)malformed|=value.find(" presentThreadCoreEquivalents=")==std::string::npos;
}
}
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void runFrame(bool oom,bool unwind=false){
    argent::FrameTiming::Totals totals;
    totals.count=119;totals.sum=119;totals.max=1;
    throwLog=!oom;allocationCountdown=oom?1:0;
    if(unwind){
        bool preserved{};
        try{argent::FrameTiming timing("failure",totals);throw 42;}catch(int error){preserved=error==42;}
        require(preserved,"Frame diagnostics replaced an exception during unwinding");
    }else{argent::FrameTiming timing("failure",totals);}
    allocationCountdown=0;throwLog=false;
    require(allocationFailures==unsigned(oom)&&logs==unsigned(!oom),"Frame report failure was not exercised");
    require(!totals.count&&!totals.sum&&!totals.max&&totals.lastReport!=argent::FrameTiming::Clock::time_point{},"Failed frame report retained its reporting window");
    totals.count=119;totals.lastReport=argent::FrameTiming::Clock::now()-std::chrono::seconds(2);
    {argent::FrameTiming timing("recovered",totals);}
    require(logs==unsigned(oom?1:2)&&!totals.count,"Frame diagnostics did not recover");
}
void seedTimeline(bool capacity){
    auto& history=argent::PresentAnalysis::history();
    const auto now=argent::PresentAnalysis::now();
    for(auto& record:history.records)record={now-20000,10000,2000,8000,true};
    history.count=capacity?history.records.size()-1:0;
    history.previousEntry=now-10000;history.previousExit=now-8000;history.previousPresent=2000;
    history.previousWorld=true;history.frame=42;history.cpuAt=now-10000;
    history.processCpu=argent::PresentAnalysis::cpu(true);history.threadCpu=argent::PresentAnalysis::cpu(false);
    history.thread=GetCurrentThreadId();history.reportAt=capacity?now:now-2000000;
}
void runTimeline(bool capacity,bool oom){
    seedTimeline(capacity);
    measureAllocations=true;
    {argent::PresentAnalysis timing(true);}
    measureAllocations=false;
    const auto allocations=allocationCalls;
    require(logs==2&&!argent::PresentAnalysis::history().count,"Normal timeline report failed");
    require(allocations>0,"Timeline report made no allocations");
    if(!oom){
        seedTimeline(capacity);throwLog=true;
        {argent::PresentAnalysis timing(true);}
        throwLog=false;
        require(logs==3&&!argent::PresentAnalysis::history().count,"Throwing timeline log was not isolated");
    }else{
        for(unsigned index=1;index<=allocations;++index){
            seedTimeline(capacity);allocationCountdown=index;
            const auto failures=allocationFailures;
            const auto previousLogs=logs;
            {argent::PresentAnalysis timing(true);}
            allocationCountdown=0;
            require(allocationFailures==failures+1,"Timeline allocation failure was not reached");
            require(!malformed&&(logs==previousLogs||logs==previousLogs+2),"Failed timeline report emitted incomplete diagnostics");
            auto& history=argent::PresentAnalysis::history();
            if(history.count)std::cerr<<"failedAllocation="<<index<<" retainedCount="<<history.count<<" capacity="<<history.records.size()<<'\n';
            require(!history.count,"Failed timeline report retained records before the next append");
            require(argent::PresentAnalysis::now()-history.reportAt<1000000,"Failed timeline report did not advance its reporting window");
            {argent::PresentAnalysis timing(false);}
            require(history.count==1,"Timeline did not accept the next frame after failure");
        }
        std::cout<<allocations<<" allocation boundaries checked\n";
    }
}
void runDisabled(){
    argent::FrameTiming::Totals totals;
    measureAllocations=true;allocationCountdown=1;throwLog=true;
    {argent::FrameTiming timing("disabled",totals);argent::PresentAnalysis analysis(true);}
    const auto pending=allocationCountdown;
    measureAllocations=false;allocationCountdown=0;throwLog=false;
    require(pending==1&&!allocationCalls&&!allocationFailures&&!logs,"Disabled diagnostics performed report work");
    require(!totals.count&&!argent::PresentAnalysis::history().frame,"Disabled diagnostics changed history");
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try{
        require(argc==2,"Expected a scenario");
        const auto mode=argv[1];const bool disabled=!std::strcmp(mode,"off");
        SetEnvironmentVariableA("ARGENT_EXTENDED_LOGGING",disabled?"0":"1");
        SetEnvironmentVariableA("ARGENT_SFS_PROFILE_TIMING","0");
        SetEnvironmentVariableA("ARGENT_PERFORMANCE_DIAGNOSTICS",std::strstr(mode,"-perf-")?"1":"0");
        argent::perf::mode=2;argent::perf::fovPhase=1;argent::perf::engineFov=120;
        if(disabled)runDisabled();
        else if(!std::strcmp(mode,"frame-log"))runFrame(false);
        else if(!std::strcmp(mode,"frame-oom"))runFrame(true);
        else if(!std::strcmp(mode,"frame-unwind"))runFrame(false,true);
        else if(!std::strcmp(mode,"timeline-capacity-oom")||!std::strcmp(mode,"timeline-perf-capacity-oom"))runTimeline(true,true);
        else if(!std::strcmp(mode,"timeline-time-oom")||!std::strcmp(mode,"timeline-perf-time-oom"))runTimeline(false,true);
        else if(!std::strcmp(mode,"timeline-log"))runTimeline(true,false);
        else throw std::runtime_error("Unknown scenario");
        std::cout<<mode<<" passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
