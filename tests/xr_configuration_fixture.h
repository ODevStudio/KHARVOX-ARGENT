#pragma once
#include "../src/openxr/GameplayMapping.h"
#include "../src/openxr/EternalMotionWheel.h"
#include "../src/openxr/HandSmoothing.h"
#include "../src/openxr/ShoulderChainsaw.h"
#include "../src/openxr/CrucibleGesture.h"
#include "../src/openxr/HandsJumpPolicy.h"
#include "../src/openxr/XInputHapticsPolicy.h"
#include <atomic>
namespace xr_configuration_fixture {
namespace input=argent::input;
namespace camera=argent::camera;
namespace presentation {inline std::atomic<uint32_t> optionBits{31};}
inline ULONGLONG tick{2000};inline bool failLog{};inline unsigned logs{};
inline bool failControlsRead{},failSupportRead{},readingSupport{};inline HANDLE observedRead{};inline unsigned readCalls{};
inline HANDLE WINAPI configurationOpen(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE other){
 readingSupport=std::filesystem::path(name).extension()==L".support";readCalls=0;
 observedRead=CreateFileW(name,access,share,security,creation,flags,other);return observedRead;
}
inline BOOL WINAPI configurationRead(HANDLE file,LPVOID output,DWORD bytes,LPDWORD read,LPOVERLAPPED overlapped){
 const bool fail=readingSupport?failSupportRead:failControlsRead;
 ++readCalls;if(fail&&readCalls==2){*read=0;SetLastError(ERROR_READ_FAULT);return FALSE;}
 return ReadFile(file,output,fail?(std::min)(bytes,DWORD(14)):bytes,read,overlapped);
}
}
#define CreateFileW xr_configuration_fixture::configurationOpen
#define ReadFile xr_configuration_fixture::configurationRead
#include "../src/openxr/ConfigurationFile.h"
#undef ReadFile
#undef CreateFileW
namespace xr_configuration_fixture {
inline ULONGLONG configurationTick(){return tick;}
inline void log(const std::string&){++logs;if(failLog)throw std::bad_alloc{};}
struct Controller {
 ULONGLONG configRead{};
 std::string configText{"profile ballista\ncinematics_3d 1\n"};
 input::WeaponConfig configuration;
 std::map<std::string,XrVector3f> supportProfiles{{"ballista",{0,0,-.4f}},{"combat_shotgun",{0,0,-.3f}}};
 input::GameplayMapping mapping;input::EternalMotionWheel motionWheel;
 input::TwoHandSupport support;std::array<kharvox::ControllerClickState,2> wheelClicks{};
 input::ShoulderChainsaw shoulder;input::CrucibleGesture physicalKill;kharvox::HandsJumpState handsJump;
 std::array<input::HandSmoothing,2> handFilters{};
 Controller(){configuration.profile="ballista";configuration.cinematics3d=true;mapping.wheelHeld=true;handFilters[0].held=true;presentation::optionBits=31;}
#define GetTickCount64 configurationTick
#include "../src/XrConfiguration.inc"
#undef GetTickCount64
};
enum class Scenario {Unchanged,ReplaceSupport,EmptySupport,MissingSupport,MalformedSupport,InvalidSupport,LockedSupport,ValidLogFailure,FlatLogFailure,InvalidLogFailure,InvalidControls,MissingControls,Throttled,ControlsReadFailure,SupportReadFailure,WritingControls,UnavailablePath,WindowsText};
inline bool sameSupport(const std::map<std::string,XrVector3f>& a,const std::map<std::string,XrVector3f>& b){
 if(a.size()!=b.size())return false;
 for(const auto& [key,v]:a){auto it=b.find(key);if(it==b.end()||v.x!=it->second.x||v.y!=it->second.y||v.z!=it->second.z)return false;}
 return true;
}
inline void run(const std::filesystem::path& root){
 wchar_t previous[32768]{};const auto previousLength=GetEnvironmentVariableW(L"ARGENT_LOG",previous,32768);
 unsigned cases{},failures{};
 for(const auto scenario:{Scenario::Unchanged,Scenario::ReplaceSupport,Scenario::EmptySupport,Scenario::MissingSupport,Scenario::MalformedSupport,Scenario::InvalidSupport,Scenario::LockedSupport,Scenario::ValidLogFailure,Scenario::FlatLogFailure,Scenario::InvalidLogFailure,Scenario::InvalidControls,Scenario::MissingControls,Scenario::Throttled,Scenario::ControlsReadFailure,Scenario::SupportReadFailure,Scenario::WritingControls,Scenario::UnavailablePath,Scenario::WindowsText}){
  const auto folder=root/("configuration-"+std::to_string(int(scenario)));std::filesystem::create_directories(folder/L"assets");
  check(SetEnvironmentVariableW(L"ARGENT_LOG",(folder/L"logs/runtime.log").c_str())!=0,"Configuration environment setup failed");
  const auto file=folder/L"assets/argent_controls.cfg";auto supportFile=file;supportFile+=L".support";
  Controller controller;if(scenario==Scenario::WindowsText)controller.configText="profile ballista\r\ncinematics_3d 1\r\n";
  const auto previousText=controller.configText;const auto previousSupport=controller.supportProfiles;
  const bool changed=scenario==Scenario::ValidLogFailure||scenario==Scenario::FlatLogFailure;
  const bool invalid=scenario==Scenario::InvalidLogFailure||scenario==Scenario::InvalidControls;
  const std::string text=scenario==Scenario::ControlsReadFailure||scenario==Scenario::WritingControls?"dominant left\nprofile crucible\n":changed?"profile crucible\ncinematics_3d "+std::string(scenario==Scenario::FlatLogFailure?"0\n":"1\n"):invalid?"profile unrecognized_profile\n":previousText;
  if(scenario!=Scenario::MissingControls){std::ofstream out(file,std::ios::binary);out<<text;out.close();check(bool(out),"Configuration fixture write failed");}
  if(scenario!=Scenario::MissingSupport){
   std::ofstream out(supportFile);
   if(scenario==Scenario::ReplaceSupport)out<<"ballista .1 0 -.5\nrocket_launcher 0 0 -.6\n";
   else if(scenario==Scenario::MalformedSupport)out<<"ballista .1 0 -.5\nrocket_launcher 0";
   else if(scenario==Scenario::InvalidSupport)out<<"ballista .1 0 -.5\nrocket_launcher 9 0 0\n";
   else if(scenario!=Scenario::EmptySupport)out<<"ballista 0 0 -.4\ncombat_shotgun 0 0 -.3\n";
   out.close();check(bool(out),"Support fixture write failed");
  }
  HANDLE locked=INVALID_HANDLE_VALUE;
  if(scenario==Scenario::LockedSupport){locked=CreateFileW(supportFile.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(locked!=INVALID_HANDLE_VALUE,"Support fixture lock failed");}
  if(scenario==Scenario::WritingControls){locked=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);check(locked!=INVALID_HANDLE_VALUE,"Configuration writer fixture failed");}
  if(scenario==Scenario::UnavailablePath)check(SetEnvironmentVariableW(L"ARGENT_LOG",nullptr)!=0,"Unavailable configuration path fixture failed");
  tick=scenario==Scenario::Throttled?500:2000;logs=0;failLog=scenario==Scenario::ValidLogFailure||scenario==Scenario::FlatLogFailure||scenario==Scenario::InvalidLogFailure;
  failControlsRead=scenario==Scenario::ControlsReadFailure;failSupportRead=scenario==Scenario::SupportReadFailure;
  bool escaped=false;try{controller.refreshConfiguration();}catch(...){escaped=true;}
  if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);failLog=failControlsRead=failSupportRead=false;
  const bool configValid=changed?(controller.configuration.profile=="crucible"&&!controller.mapping.wheelHeld&&!controller.handFilters[0].held&&controller.configText==text&&presentation::optionBits==(scenario==Scenario::FlatLogFailure?15u:31u)):
   (controller.configuration.profile=="ballista"&&!controller.configuration.leftHanded&&controller.mapping.wheelHeld&&controller.handFilters[0].held&&controller.configText==(invalid?text:previousText)&&presentation::optionBits==31u);
  const bool supportValid=scenario==Scenario::ReplaceSupport?(controller.supportProfiles.size()==2&&controller.supportProfiles.count("rocket_launcher")&&controller.supportProfiles.at("ballista").x==.1f):
   scenario==Scenario::EmptySupport||scenario==Scenario::MissingSupport?controller.supportProfiles.empty():sameSupport(controller.supportProfiles,previousSupport);
  const bool throttleValid=scenario!=Scenario::Throttled||(controller.configRead==0&&logs==0);
  ++cases;if(escaped||!configValid||!supportValid||!throttleValid){++failures;std::cerr<<"Configuration reload scenario "<<int(scenario)<<" escaped="<<escaped<<" config="<<configValid<<" support="<<supportValid<<" throttle="<<throttleValid<<'\n';}
 }
 const auto file=root/L"configuration-native-read.cfg";const std::string bytes="dominant left\n"+std::string(8193,'#')+"\r\n";
 {std::ofstream out(file,std::ios::binary);out<<bytes;out.close();check(bool(out),"Native read fixture write failed");}
 for(const bool fail:{false,true}){
  failControlsRead=fail;std::string content="previous";const auto result=input::readConfigurationFile(file,content);const auto calls=readCalls;failControlsRead=false;
  DWORD flags{};const bool closed=!GetHandleInformation(observedRead,&flags);
  const bool passed=closed&&(fail?(result==ERROR_READ_FAULT&&content=="previous"&&calls==2):(result==ERROR_SUCCESS&&content==bytes&&calls==4));
  ++cases;if(!passed){++failures;std::cerr<<"Native configuration read failure="<<fail<<" closed="<<closed<<" calls="<<calls<<'\n';}
 }
 SetEnvironmentVariableW(L"ARGENT_LOG",previousLength&&previousLength<32768?previous:nullptr);
 std::cout<<cases<<" configuration reload scenarios, "<<failures<<" failures\n";check(!failures,"Configuration reload boundaries failed");
}
}
