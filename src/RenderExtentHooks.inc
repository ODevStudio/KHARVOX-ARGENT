namespace {
uint32_t eyeWidth{},eyeHeight{};
using ClientRect=BOOL(WINAPI*)(HWND,LPRECT);
ClientRect nativeClientRect{};uintptr_t renderImageBase{};
using RefreshExtent=void(__fastcall*)();RefreshExtent nativeRefreshExtent{};
void __fastcall refreshRenderExtent(){
 auto output=reinterpret_cast<uint32_t*>(renderImageBase+build::rva(0x39aabe4));
 output[0]=eyeWidth;output[1]=eyeHeight;
 nativeRefreshExtent();
}
BOOL WINAPI renderClientRect(HWND window,LPRECT rect){
 const auto caller=build::semanticRva(reinterpret_cast<uintptr_t>(_ReturnAddress())-renderImageBase);
 const auto ok=nativeClientRect(window,rect);
 const bool swapchainCaller=caller==0x1d09134||caller==0x1d091ba;
 if(ok&&rect&&swapchainCaller&&rect->right>rect->left&&rect->bottom>rect->top){
  rect->right=rect->left+LONG(eyeWidth);rect->bottom=rect->top+LONG(eyeHeight);
 }
 return ok;
}
uint32_t __fastcall renderWidth(void*){return eyeWidth;}
uint32_t __fastcall renderHeight(void*){return eyeHeight;}
struct RenderExtentSetup {
 void* targets[3];size_t created{};
 void** slot{};void* previous{};DWORD protection{};
 uint32_t outputBefore[2]{};
 bool writable{},patched{},refreshed{},committed{};
 bool restoreProtection() noexcept {
  if(!writable)return true;
  DWORD ignored{};if(!VirtualProtect(slot,sizeof(void*),protection,&ignored))return false;
  writable=false;return true;
 }
 ~RenderExtentSetup(){
  if(committed)return;
  if(patched){
   if(!writable){DWORD ignored{};if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&ignored))failCameraHookRollback();writable=true;}
   InterlockedCompareExchangePointer(slot,previous,reinterpret_cast<void*>(&renderClientRect));
  }
  if(!restoreProtection())failCameraHookRollback();
  while(created)removeOwnedCameraHook(targets[--created]);
  if(refreshed){
   auto output=reinterpret_cast<uint32_t*>(renderImageBase+build::rva(0x39aabe4));
   if(output[0]==eyeWidth)output[0]=outputBefore[0];if(output[1]==eyeHeight)output[1]=outputBefore[1];
  }
  eyeWidth=eyeHeight=0;
 }
};
}
bool installRenderExtent(uint32_t width,uint32_t height) noexcept {try{
 std::lock_guard<std::mutex> installationLock(installationMutex);
 if(eyeWidth)return eyeWidth==width&&eyeHeight==height;
 if(!width||!height||width>uint32_t(LONG_MAX)||height>uint32_t(LONG_MAX)||!approvedImage())return false;
 auto base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
 constexpr unsigned char w[]={0x8b,0x05,0x0e,0xb3,0xce,0x01,0xc3,0xcc};
 constexpr unsigned char h[]={0x8b,0x05,0x22,0xb3,0xce,0x01,0xc3,0xcc};
 const bool store=approvedStoreImage();
 auto wt=base+build::rva(0x1cbf8d0);auto ht=base+build::rva(0x1cbf8c0);
 constexpr unsigned char storeW[]={0x8b,0x05,0x8e,0x0e,0xd1,0x01,0xc3,0xcc};
 constexpr unsigned char storeH[]={0x8b,0x05,0xa2,0x0e,0xd1,0x01,0xc3,0xcc};
 if(store ? (std::memcmp(wt,storeW,sizeof(storeW))||std::memcmp(ht,storeH,sizeof(storeH)))
          : (std::memcmp(wt,w,sizeof(w))||std::memcmp(ht,h,sizeof(h))))return false;
 constexpr unsigned char rect1[]={0xff,0x15,0x14,0x2f,0xd1,0x00};
 constexpr unsigned char rect2[]={0xff,0x15,0x8e,0x2e,0xd1,0x00};
 constexpr unsigned char storeRect1[]={0xff,0x15,0x34,0x5c,0xd2,0x00};
 constexpr unsigned char storeRect2[]={0xff,0x15,0xae,0x5b,0xd2,0x00};
 if(store ? (std::memcmp(base+0x1d9568e,storeRect1,6)||std::memcmp(base+0x1d95714,storeRect2,6))
          : (std::memcmp(base+0x1d0912e,rect1,6)||std::memcmp(base+0x1d091b4,rect2,6)))return false;
 constexpr unsigned char refresh[]={0x48,0x83,0xec,0x28,0x80,0x3d,0xe7,0xf1,0x9b,0x04,0x01,0x75,0x16};
 constexpr unsigned char storeRefresh[]={0x48,0x83,0xec,0x28,0x80,0x3d,0x57,0xa6,0x9e,0x04,0x01,0x75,0x16};
 auto rt=base+build::rva(0x1cbfa60);
 if(store?std::memcmp(rt,storeRefresh,sizeof(storeRefresh)):std::memcmp(rt,refresh,sizeof(refresh)))return false;
 auto slot=reinterpret_cast<void**>(base+build::rva(0x2a1c048));const auto previous=*slot;
 if(!previous||previous==reinterpret_cast<void*>(&renderClientRect))return false;
 const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 RenderExtentSetup setup{{wt,ht,rt}};
 void* const detours[]{reinterpret_cast<void*>(&renderWidth),reinterpret_cast<void*>(&renderHeight),reinterpret_cast<void*>(&refreshRenderExtent)};
 void** const originals[]{nullptr,nullptr,reinterpret_cast<void**>(&nativeRefreshExtent)};
 for(;setup.created<std::size(setup.targets);++setup.created)
  if(MH_CreateHook(setup.targets[setup.created],detours[setup.created],originals[setup.created])!=MH_OK)return false;
 eyeWidth=width;eyeHeight=height;renderImageBase=reinterpret_cast<uintptr_t>(base);
 nativeClientRect=reinterpret_cast<ClientRect>(previous);setup.slot=slot;setup.previous=previous;
 if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&setup.protection))return false;
 setup.writable=true;
 if(InterlockedCompareExchangePointer(slot,reinterpret_cast<void*>(&renderClientRect),previous)!=previous)return false;
 setup.patched=true;
 if(!setup.restoreProtection())return false;
 for(auto hook:setup.targets)if(MH_EnableHook(hook)!=MH_OK)return false;
 auto output=reinterpret_cast<const uint32_t*>(renderImageBase+build::rva(0x39aabe4));
 setup.outputBefore[0]=output[0];setup.outputBefore[1]=output[1];setup.refreshed=true;
 refreshRenderExtent();setup.committed=true;
 try{log("ETERNAL_RENDER_EXTENT "+std::to_string(width)+"x"+std::to_string(height)+" engineAccessors=1 swapchainClientQueries=2 sceneRefresh=1");}catch(...){}
 return true;
 }catch(...){return false;}}
