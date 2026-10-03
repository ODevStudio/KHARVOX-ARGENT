#include "../src/RenderTrace.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
VkResult nativeResult{};
unsigned nativeCalls{},traceResets{},cameraResets{};
bool recorded=true;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T handle(uintptr_t value){return reinterpret_cast<T>(value);}
VKAPI_ATTR VkResult VKAPI_CALL resetCommand(VkCommandBuffer command,VkCommandBufferResetFlags flags){check(command==handle<VkCommandBuffer>(3)&&flags==VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT,"Command reset arguments changed");++nativeCalls;return nativeResult;}
VKAPI_ATTR VkResult VKAPI_CALL resetPool(VkDevice device,VkCommandPool pool,VkCommandPoolResetFlags flags){check(device==handle<VkDevice>(1)&&pool==handle<VkCommandPool>(2)&&flags==VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT,"Pool reset arguments changed");++nativeCalls;return nativeResult;}
struct Camera {
    void command(VkCommandBuffer,VkCommandPool={}){++cameraResets;recorded=false;}
    void resetPool(VkCommandPool,bool destroy){check(!destroy,"Reset retired the camera pool");++cameraResets;recorded=false;}
    void freeCommand(VkCommandBuffer){}
    void bindPipeline(VkCommandBuffer,VkPipeline,VkPipelineBindPoint){}
    void draw(VkCommandBuffer){}
    void dispatch(VkCommandBuffer){}
};
struct State {
    bool game=true;Camera camera;
    template<class T>T proc(const char* name)const{
        if(!std::strcmp(name,"vkResetCommandBuffer"))return reinterpret_cast<T>(resetCommand);
        if(!std::strcmp(name,"vkResetCommandPool"))return reinterpret_cast<T>(resetPool);
        throw std::runtime_error("Unexpected reset dispatch");
    }
};
const auto state=std::make_shared<State>();
template<class T>void* key(T){return nullptr;}
std::shared_ptr<State> deviceOf(void*){return state;}
State* commandDeviceOf(void*){return state.get();}
template<class S,class F>void observeCamera(const S& source,F&& action)noexcept{try{if(source->game)action(source->camera);}catch(...){}}
}
namespace argent::trace {
void reset(VkCommandBuffer,VkCommandPool)noexcept{++traceResets;}
void resetPool(VkCommandPool,bool)noexcept{++traceResets;}
void forget(VkCommandBuffer)noexcept{}
void record(VkCommandBuffer,const char*,std::array<uint64_t,6>)noexcept{}
void object(VkDevice,const char*,uint64_t,std::array<uint64_t,6>)noexcept{}
}
#define ARGENT_COMMAND_PROC(source,api) source->proc<PFN_##api>(#api)
#include "../src/RenderTraceHooks.inc"
#undef ARGENT_COMMAND_PROC

int main(){try{
    unsigned scenarios{},failures{};
    for(const auto result:{VK_SUCCESS,VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_ERROR_INITIALIZATION_FAILED,VK_ERROR_DEVICE_LOST})for(const bool game:{false,true})for(const bool pool:{false,true}){
        ++scenarios;nativeResult=result;state->game=game;nativeCalls=traceResets=cameraResets=0;recorded=true;
        const auto returned=pool?vkResetCommandPool(handle<VkDevice>(1),handle<VkCommandPool>(2),VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT):vkResetCommandBuffer(handle<VkCommandBuffer>(3),VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
        const auto expected=unsigned(result==VK_SUCCESS&&game);
        const bool passed=returned==result&&nativeCalls==1&&traceResets==expected&&cameraResets==expected&&recorded==!expected;
        failures+=!passed;
        if(!passed)std::cerr<<"result="<<result<<" game="<<game<<" pool="<<pool<<" changed capture state on failed reset\n";
    }
    std::cout<<scenarios<<" production reset-hook scenarios, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
