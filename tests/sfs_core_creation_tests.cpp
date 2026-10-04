#include "../src/sfs/StereoResources.h"
#include "../src/sfs/NativeDispatch.h"
#include "../src/hands/HandSceneDepthTracker.cpp"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace {
unsigned allocationCountdown{},allocations{},allocationFailures{};
bool measuring{};
}
void* operator new(std::size_t size){
    if(measuring)++allocations;
    if(allocationCountdown&&!--allocationCountdown){++allocationFailures;throw std::bad_alloc{};}
    if(auto* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc{};
}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}

namespace {
enum class Kind {Image,View,Pass,Framebuffer};
struct Resource {uintptr_t handle{};Kind kind{};bool live{};};
std::array<Resource,32> resources;
std::array<unsigned,4> nativeCalls{};
unsigned resourceCount{},invalidDestructions{},lifetimeRetirements{},failedCall{},stateFailure{};
Kind failedKind{};
bool nullNative{},logThrows{},mixedMode=true,extendedView{},receivedPassStereo{};
VkAllocationCallbacks allocator{};
VkImageCreateInfo receivedImage{};
VkImageViewCreateInfo receivedView{};
VkFramebufferCreateInfo receivedFramebuffer{};
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
template<class T>T handle(uintptr_t value){return reinterpret_cast<T>(value);}
unsigned live(){unsigned count{};for(unsigned i=0;i<resourceCount;++i)count+=resources[i].live;return count;}
template<class T>VkResult nativeCreate(Kind kind,const VkAllocationCallbacks* a,T* out){
    require(a==&allocator,"Native creation lost the caller allocator");
    const auto call=++nativeCalls[unsigned(kind)];
    if(failedCall&&failedKind==kind&&failedCall==call){
        *out=handle<T>(nullNative?0:0xdead);
        return nullNative?VK_SUCCESS:VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    const auto value=0x1000+16*resourceCount;resources[resourceCount++]={value,kind,true};
    *out=handle<T>(value);return VK_SUCCESS;
}
template<class T>void nativeDestroy(Kind kind,const VkAllocationCallbacks* a,T h){
    if(a!=&allocator){++invalidDestructions;return;}
    for(unsigned i=0;i<resourceCount;++i)if(resources[i].handle==reinterpret_cast<uintptr_t>(h)&&resources[i].kind==kind&&resources[i].live){resources[i].live=false;return;}
    ++invalidDestructions;
}
VKAPI_ATTR VkResult VKAPI_CALL nativeImage(VkDevice,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* out){receivedImage=*i;return nativeCreate(Kind::Image,a,out);}
VKAPI_ATTR void VKAPI_CALL nativeImageDestroy(VkDevice,VkImage h,const VkAllocationCallbacks* a){nativeDestroy(Kind::Image,a,h);}
VKAPI_ATTR VkResult VKAPI_CALL nativeView(VkDevice,const VkImageViewCreateInfo* i,const VkAllocationCallbacks* a,VkImageView* out){receivedView=*i;return nativeCreate(Kind::View,a,out);}
VKAPI_ATTR void VKAPI_CALL nativeViewDestroy(VkDevice,VkImageView h,const VkAllocationCallbacks* a){nativeDestroy(Kind::View,a,h);}
VKAPI_ATTR VkResult VKAPI_CALL nativePass(VkDevice,const VkRenderPassCreateInfo* i,const VkAllocationCallbacks* a,VkRenderPass* out){
    require(i->pAttachments[0].initialLayout==VK_IMAGE_LAYOUT_GENERAL&&i->pAttachments[0].finalLayout==VK_IMAGE_LAYOUT_GENERAL,"Source-ring pass layout was not promoted");
    receivedPassStereo=i->pNext!=nullptr;
    if(receivedPassStereo){auto* mv=static_cast<const VkRenderPassMultiviewCreateInfo*>(i->pNext);require(mv->pViewMasks[0]==3,"Stereo render pass lost its multiview mask");}
    return nativeCreate(Kind::Pass,a,out);
}
VKAPI_ATTR void VKAPI_CALL nativePassDestroy(VkDevice,VkRenderPass h,const VkAllocationCallbacks* a){nativeDestroy(Kind::Pass,a,h);}
VKAPI_ATTR VkResult VKAPI_CALL nativeFramebuffer(VkDevice,const VkFramebufferCreateInfo* i,const VkAllocationCallbacks* a,VkFramebuffer* out){receivedFramebuffer=*i;return nativeCreate(Kind::Framebuffer,a,out);}
VKAPI_ATTR void VKAPI_CALL nativeFramebufferDestroy(VkDevice,VkFramebuffer h,const VkAllocationCallbacks* a){nativeDestroy(Kind::Framebuffer,a,h);}
struct Capture {
    std::map<VkImage,VkImageCreateInfo> images;
    std::map<VkImageView,VkImageViewCreateInfo> views;
    std::map<VkRenderPass,std::vector<VkAttachmentDescription>> passes;
    std::map<VkFramebuffer,std::vector<VkImageView>> framebuffers;
    void image(VkImage h,const VkImageCreateInfo& i){images[h]=i;}
    void forgetImage(VkImage h){images.erase(h);}
    void view(VkImageView h,const VkImageViewCreateInfo& i){views[h]=i;}
    void forgetView(VkImageView h){views.erase(h);}
    void renderPass(VkRenderPass h,const VkRenderPassCreateInfo& i){passes[h].assign(i.pAttachments,i.pAttachments+i.attachmentCount);}
    void forgetPass(VkRenderPass h){passes.erase(h);}
    void framebuffer(VkFramebuffer h,const VkFramebufferCreateInfo& i){framebuffers[h].assign(i.pAttachments,i.pAttachments+i.attachmentCount);}
    void forgetFramebuffer(VkFramebuffer h){framebuffers.erase(h);}
    bool empty()const{return images.empty()&&views.empty()&&passes.empty()&&framebuffers.empty();}
};
struct State {
    std::shared_mutex mutex;
    kharvox::sfs::Images images;
    kharvox::sfs::NativeDispatch dispatch;
    std::unique_ptr<Capture> waterCapture;
    bool sources=true;
    std::unordered_map<VkImageView,uint32_t> viewLayers;
    std::unordered_map<VkImageView,VkImageViewCreateInfo> viewInfos;
    std::unordered_map<VkImageView,std::array<VkImageView,2>> eyeViews;
    std::unordered_map<VkRenderPass,VkRenderPass> passes;
    std::unordered_map<VkFramebuffer,bool> framebufferStereo,mixedFramebuffers;
    std::atomic<uint64_t> passRetirement{1},framebufferRetirement{1};
    uint32_t mixedDiagnostics{};
};
std::unique_ptr<State> current;
State* state(VkDevice){if(stateFailure==1)throw std::runtime_error("state unavailable");if(stateFailure==2)throw std::bad_alloc{};if(stateFailure==3)throw 7;return current.get();}
void note(const std::string&){if(logThrows)throw 7;}
void waterCaptureImage(VkDevice,VkImage,const VkImageCreateInfo&)noexcept{}
void waterCaptureView(VkDevice,VkImageView,const VkImageViewCreateInfo&)noexcept{}
}
namespace kharvox {
struct Lifetime {
    template<class T>static uintptr_t key(T h){return reinterpret_cast<uintptr_t>(h);}
    template<class F>void retire(uintptr_t,F&& action){++lifetimeRetirements;action();}
};
using GameImageLifetime=Lifetime;
Lifetime& gameImageLifetime(){static Lifetime value;return value;}
}
namespace {
using namespace kharvox::sfs;
using kharvox::sfs::NativeDispatch;
namespace handDepth=kharvox::hands;
#define FN(name) NativeDispatch::require(s->dispatch.name,#name)
#include "../src/sfs/ResultBoundary.inc"
#include "../src/sfs/CoreResources.inc"
#undef FN
#undef RESULT_BEGIN
#undef RESULT_END

template<class T>void clear(T& value){T fresh;value.swap(fresh);}
void reset(bool capture){
    allocationCountdown=allocations=allocationFailures=0;measuring=false;
    current=std::make_unique<State>();
    if(capture)current->waterCapture=std::make_unique<Capture>();
    auto& d=current->dispatch;d.vkCreateImage=nativeImage;d.vkDestroyImage=nativeImageDestroy;
    d.vkCreateImageView=nativeView;d.vkDestroyImageView=nativeViewDestroy;
    d.vkCreateRenderPass=nativePass;d.vkDestroyRenderPass=nativePassDestroy;
    d.vkCreateFramebuffer=nativeFramebuffer;d.vkDestroyFramebuffer=nativeFramebufferDestroy;
    current->images.track(handle<VkImage>(0x60),2);
    current->passes[handle<VkRenderPass>(0x40)]=handle<VkRenderPass>(0x41);
    current->viewLayers[handle<VkImageView>(0x50)]=2;current->viewLayers[handle<VkImageView>(0x51)]=mixedMode?1:2;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=handle<VkImage>(0x60);
    current->viewInfos[handle<VkImageView>(0x50)]=view;
    handDepth::handSceneDeviceDestroyed();clear(handDepth::images);clear(handDepth::views);clear(handDepth::renderPasses);clear(handDepth::framebuffers);
    resources={};nativeCalls={};resourceCount=invalidDestructions=lifetimeRetirements=failedCall=stateFailure=0;
    nullNative=logThrows=extendedView=receivedPassStereo=false;
}
struct Outcome {VkResult result{};uintptr_t output{};bool escaped{};};
Outcome invoke(Kind kind){
    Outcome result;const auto d=handle<VkDevice>(1);
    try{switch(kind){
    case Kind::Image:{VkImageCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};i.imageType=VK_IMAGE_TYPE_2D;i.arrayLayers=1;i.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;i.extent={64,64,1};i.samples=VK_SAMPLE_COUNT_1_BIT;VkImage h=handle<VkImage>(0xbeef);result.result=createImage(d,&i,&allocator,&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::View:{VkImageViewCreateInfo i{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};i.image=handle<VkImage>(0x60);i.viewType=VK_IMAGE_VIEW_TYPE_2D;i.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};VkBaseInStructure extension{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};if(extendedView)i.pNext=&extension;VkImageView h=handle<VkImageView>(0xbeef);result.result=createView(d,&i,&allocator,&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::Pass:{VkAttachmentDescription attachment{};attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;VkSubpassDescription subpass{};VkRenderPassCreateInfo i{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};i.attachmentCount=1;i.pAttachments=&attachment;i.subpassCount=1;i.pSubpasses=&subpass;VkRenderPass h=handle<VkRenderPass>(0xbeef);result.result=createPass(d,&i,&allocator,&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    case Kind::Framebuffer:{std::array<VkImageView,2> views{handle<VkImageView>(0x50),handle<VkImageView>(0x51)};VkFramebufferCreateInfo i{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};i.renderPass=handle<VkRenderPass>(0x40);i.attachmentCount=uint32_t(views.size());i.pAttachments=views.data();i.width=i.height=64;i.layers=1;VkFramebuffer h=handle<VkFramebuffer>(0xbeef);result.result=createFramebuffer(d,&i,&allocator,&h);result.output=reinterpret_cast<uintptr_t>(h);break;}
    }}catch(...){result.escaped=true;}
    return result;
}
void destroy(Kind kind,uintptr_t output){
    const auto d=handle<VkDevice>(1);
    switch(kind){case Kind::Image:destroyImage(d,handle<VkImage>(output),&allocator);break;case Kind::View:destroyView(d,handle<VkImageView>(output),&allocator);break;case Kind::Pass:destroyPass(d,handle<VkRenderPass>(output),&allocator);break;case Kind::Framebuffer:destroyFramebuffer(d,handle<VkFramebuffer>(output),&allocator);break;}
}
void clean(){
    require(!live()&&!invalidDestructions,"Native rollback leaked or destroyed an undefined output");
    require(handDepth::images.empty()&&handDepth::views.empty()&&handDepth::renderPasses.empty()&&handDepth::framebuffers.empty(),"Hand metadata retained an unpublished resource");
    require(!current->waterCapture||current->waterCapture->empty(),"Capture metadata retained an unpublished resource");
    require(current->viewLayers.size()==2&&current->viewInfos.size()==1&&current->passes.size()==1&&current->framebufferStereo.empty()&&current->mixedFramebuffers.empty(),"SFS metadata retained an unpublished resource or removed dependencies");
    require(current->images.layers(handle<VkImage>(0x60))==2,"Rollback removed input-image metadata");
    for(unsigned i=0;i<resourceCount;++i)if(resources[i].kind==Kind::Image)require(current->images.layers(handle<VkImage>(resources[i].handle))==0,"Image registry retained a failed creation");
}
void published(Kind kind,uintptr_t output){
    require(output&&output!=0xbeef&&output!=0xdead,"Successful creation did not publish its owned output");
    require(live()==(kind==Kind::Pass?2u:1u),"Successful creation lost native ownership");
    if(kind==Kind::Image){require(receivedImage.arrayLayers==2&&(receivedImage.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT),"Depth image lost promotion or transfer usage");require(current->images.layers(handle<VkImage>(output))==2,"Image registry lost stereo classification");}
    if(kind==Kind::View){require(receivedView.viewType==VK_IMAGE_VIEW_TYPE_2D_ARRAY&&receivedView.subresourceRange.layerCount==2,"Shader view lost stereo promotion");require(current->viewLayers.at(handle<VkImageView>(output))==2,"View registry lost stereo classification");require(current->viewInfos.count(handle<VkImageView>(output))==!extendedView,"Extended view stored dangling pNext metadata");}
    if(kind==Kind::Pass)require(receivedPassStereo&&current->passes.count(handle<VkRenderPass>(output)),"Render pass lost its owned stereo variant");
    if(kind==Kind::Framebuffer){require(receivedFramebuffer.renderPass==handle<VkRenderPass>(mixedMode?0x40:0x41),"Framebuffer selected the wrong render pass");require(current->framebufferStereo.at(handle<VkFramebuffer>(output))==!mixedMode,"Framebuffer lost stereo classification");require(current->mixedFramebuffers.at(handle<VkFramebuffer>(output))==mixedMode,"Framebuffer lost mixed classification");}
    destroy(kind,output);clean();
}
void failed(Kind kind,const Outcome& outcome,VkResult expected){
    clean();
    require(!outcome.escaped,"Creation exception escaped its Vulkan boundary");
    require(outcome.result==expected,"Creation returned the wrong error category");
    require(!outcome.output,"Failed creation published a native output");
    require(!lifetimeRetirements&&current->passRetirement==1&&current->framebufferRetirement==1,"Unpublished rollback used published-resource retirement");
    failedCall=stateFailure=0;logThrows=false;const auto retry=invoke(kind);
    require(!retry.escaped&&retry.result==VK_SUCCESS,"Failed creation prevented a clean retry");published(kind,retry.output);
}
unsigned hostAllocation(Kind kind,bool capture,unsigned boundary=0){
    reset(capture);allocationCountdown=boundary;measuring=!boundary;const auto result=invoke(kind);
    allocationCountdown=0;measuring=false;const auto count=allocations;
    if(boundary)require(allocationFailures==1,"Host allocation injection was not reached");
    if(result.result==VK_SUCCESS&&!result.escaped)published(kind,result.output);
    else failed(kind,result,VK_ERROR_OUT_OF_HOST_MEMORY);
    return count;
}
void missingDispatch(Kind kind,bool destruction){
    reset(true);auto& d=current->dispatch;
    switch(kind){case Kind::Image:if(destruction)d.vkDestroyImage=nullptr;else d.vkCreateImage=nullptr;break;case Kind::View:if(destruction)d.vkDestroyImageView=nullptr;else d.vkCreateImageView=nullptr;break;case Kind::Pass:if(destruction)d.vkDestroyRenderPass=nullptr;else d.vkCreateRenderPass=nullptr;break;case Kind::Framebuffer:if(destruction)d.vkDestroyFramebuffer=nullptr;else d.vkCreateFramebuffer=nullptr;break;}
    const auto result=invoke(kind);require(resourceCount==0,"Missing cleanup dispatch was discovered after native allocation");require(!result.escaped&&result.result==VK_ERROR_INITIALIZATION_FAILED&&!result.output,"Missing dispatch did not reject setup cleanly");clean();
}
void invalidArguments(Kind kind){
    reset(true);const auto d=handle<VkDevice>(1);VkResult result{},missingOutput{};uintptr_t output=0xbeef;
    switch(kind){
    case Kind::Image:{VkImageCreateInfo i{};result=createImage(d,nullptr,&allocator,reinterpret_cast<VkImage*>(&output));missingOutput=createImage(d,&i,&allocator,nullptr);break;}
    case Kind::View:{VkImageViewCreateInfo i{};result=createView(d,nullptr,&allocator,reinterpret_cast<VkImageView*>(&output));missingOutput=createView(d,&i,&allocator,nullptr);break;}
    case Kind::Pass:{VkRenderPassCreateInfo i{};result=createPass(d,nullptr,&allocator,reinterpret_cast<VkRenderPass*>(&output));missingOutput=createPass(d,&i,&allocator,nullptr);break;}
    case Kind::Framebuffer:{VkFramebufferCreateInfo i{};result=createFramebuffer(d,nullptr,&allocator,reinterpret_cast<VkFramebuffer*>(&output));missingOutput=createFramebuffer(d,&i,&allocator,nullptr);break;}
    }
    require(result==VK_ERROR_INITIALIZATION_FAILED&&missingOutput==result&&!output&&!resourceCount,"Invalid creation arguments were not rejected before native setup");clean();
}
unsigned scenarios{},failures{};
template<class F>void run(const char* label,F&& action){++scenarios;try{action();}catch(const std::exception& e){allocationCountdown=0;measuring=false;++failures;std::cerr<<label<<": "<<e.what()<<'\n';}catch(...){allocationCountdown=0;measuring=false;++failures;std::cerr<<label<<": unknown exception\n";}}
}
int main(int argc,char** argv){
    const bool extended=argc==2&&!std::strcmp(argv[1],"--extended");
    for(auto kind:{Kind::Image,Kind::View,Kind::Pass,Kind::Framebuffer}){
        for(bool capture:{false,true}){
            unsigned count{};run("host measurement",[&]{count=hostAllocation(kind,capture);});
            for(unsigned i=1;i<=count;++i)run("host allocation rollback",[&]{hostAllocation(kind,capture,i);});
        }
        for(bool destruction:{false,true})run("missing dispatch",[&]{missingDispatch(kind,destruction);});
        for(unsigned call=1;call<=(kind==Kind::Pass?2u:1u);++call){
            run("native failure",[&]{reset(true);failedKind=kind;failedCall=call;failed(kind,invoke(kind),VK_ERROR_OUT_OF_DEVICE_MEMORY);});
            if(extended)run("null successful native output",[&]{reset(true);failedKind=kind;failedCall=call;nullNative=true;failed(kind,invoke(kind),VK_ERROR_INITIALIZATION_FAILED);});
        }
        for(unsigned error=1;error<=3;++error)run("state failure with broken diagnostics",[&]{reset(true);stateFailure=error;logThrows=true;failed(kind,invoke(kind),error==2?VK_ERROR_OUT_OF_HOST_MEMORY:VK_ERROR_INITIALIZATION_FAILED);});
        if(extended)run("invalid arguments",[&]{invalidArguments(kind);});
    }
    for(bool capture:{false,true})run("optional mixed diagnostics",[&]{reset(capture);logThrows=true;const auto result=invoke(Kind::Framebuffer);require(!result.escaped&&result.result==VK_SUCCESS,"Optional diagnostics failed a complete framebuffer");published(Kind::Framebuffer,result.output);});
    if(extended){
        run("extended view",[]{reset(true);extendedView=true;const auto result=invoke(Kind::View);require(!result.escaped&&result.result==VK_SUCCESS,"Extended view failed");published(Kind::View,result.output);});
        run("all-stereo framebuffer",[]{mixedMode=false;reset(true);const auto result=invoke(Kind::Framebuffer);require(!result.escaped&&result.result==VK_SUCCESS,"All-stereo framebuffer failed");published(Kind::Framebuffer,result.output);mixedMode=true;});
    }
    std::cout<<scenarios<<" SFS core creation scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
