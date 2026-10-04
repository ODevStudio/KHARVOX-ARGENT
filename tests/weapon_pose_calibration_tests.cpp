#include "../src/hands/WeaponPoseCalibration.h"
#include "../src/openxr/SupportCalibration.h"
#include "../src/weapon/EternalHapticsWeapon.h"
#include "../src/WeaponAttachmentCalibration.h"
#include "../src/WeaponIdlePose.h"
#include <stdexcept>
#include <iostream>
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
class FailedConfigurationBuffer : public std::streambuf {
 std::string text;
public:
 explicit FailedConfigurationBuffer(std::string value):text(std::move(value)){setg(text.data(),text.data(),text.data()+text.size());}
 int_type underflow()override{throw std::ios_base::failure("Configuration read failure");}
};
void checkSupportPersistence(const std::filesystem::path& root){
 using namespace argent::input;
 const auto folder=root/L"support";std::filesystem::create_directories(folder);
 const auto config=folder/L"argent_controls.cfg";auto saved=config;saved+=L".support";auto temporary=saved;temporary+=L".tmp";
 std::map<std::string,XrVector3f> profiles{{"ballista",{0,0,-.4f}},{"rocket_launcher",{0,0,-.6f}}};
 const auto equal=[](const auto& a,const auto& b){
  if(a.size()!=b.size())return false;
  for(const auto& [key,v]:a){auto it=b.find(key);if(it==b.end()||v.x!=it->second.x||v.y!=it->second.y||v.z!=it->second.z)return false;}
  return true;
 };
 const auto contents=[&](){std::ifstream in(saved);return std::string(std::istreambuf_iterator<char>(in),{});};
 unsigned cases{},failures{};
 const auto expect=[&](bool passed,const char* name){++cases;if(!passed){++failures;std::cerr<<name<<": support persistence failed\n";}};
 expect(saveSupportCalibration(config,"ballista",{.1f,0,-.5f},profiles)&&profiles.at("ballista").x==.1f&&profiles.at("rocket_launcher").z==-.6f,"initial save");
 const auto committed=contents();
 const auto locked=CreateFileW(saved.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 check(locked!=INVALID_HANDLE_VALUE,"Support target lock failed");
 auto replacement=profiles;const bool replaced=saveSupportCalibration(config,"ballista",{.2f,0,-.5f},replacement);CloseHandle(locked);
 expect(!replaced&&equal(replacement,profiles)&&contents()==committed,"replacement failure");
 std::filesystem::remove(temporary);std::filesystem::create_directory(temporary);
 auto unwritable=profiles;const bool written=saveSupportCalibration(config,"combat_shotgun",{.2f,0,-.5f},unwritable);
 expect(!written&&equal(unwritable,profiles)&&contents()==committed,"temporary open failure");
 std::filesystem::remove(temporary);
 const auto previousDirectory=std::filesystem::current_path();std::filesystem::current_path(folder);
 auto unavailable=profiles;const bool relative=saveSupportCalibration({},"ballista",{.2f,0,-.5f},unavailable);
 const bool noRelativeFiles=!std::filesystem::exists(L".support")&&!std::filesystem::exists(L".support.tmp");std::filesystem::current_path(previousDirectory);
 expect(!relative&&equal(unavailable,profiles)&&noRelativeFiles,"unavailable configuration path");
 expect(saveSupportCalibration(config,"ballista",{.2f,0,-.5f},profiles)&&profiles.at("ballista").x==.2f&&!std::filesystem::exists(temporary),"successful retry");
 std::ifstream in(saved);std::map<std::string,XrVector3f> disk;std::string name;XrVector3f value{};
 while(in>>name>>value.x>>value.y>>value.z)disk[name]=value;
 expect(in.eof()&&equal(disk,profiles),"complete round trip");
 std::cout<<cases<<" support persistence scenarios, "<<failures<<" failures\n";check(!failures,"Support persistence boundaries failed");
}
int main(){try{
 using namespace argent::input;
 {
  // Shared parent remains at its native baseline while one weapon's final
  // attachment moves. Switching away must reject the preceding XR sample.
  using namespace argent::player;
  check(matchingWeaponCalibration("crucible","crucible")&&!matchingWeaponCalibration("crucible","super_shotgun")&&
   !matchingWeaponCalibration("","crucible"),"Previous weapon correction accepted after switch");
  const float identity[]{1,0,0,0,1,0,0,0,1},turned[]{0,1,0,-1,0,0,0,0,1};
  const float from[]{3,4,5},to[]{4,6,8};float origin[]{5,4,5},axis[]{1,0,0,0,1,0,0,0,1};
  check(calibrateWeaponAttachment(from,identity,to,turned,origin,axis),"Attachment correction failed");
  check(std::abs(origin[0]-4)<1e-6&&std::abs(origin[1]-8)<1e-6&&std::abs(origin[2]-8)<1e-6,"Attachment did not rotate around controller pivot");
  for(int i=0;i<9;++i)check(std::abs(axis[i]-turned[i])<1e-6,"Attachment rotation incorrect");
  check(calibrateWeaponAttachment(to,turned,from,identity,origin,axis),"Inverse attachment correction failed");
  check(std::abs(origin[0]-5)<1e-6&&std::abs(origin[1]-4)<1e-6&&std::abs(origin[2]-5)<1e-6,"Correction accumulated in model frame");
  for(int i=0;i<9;++i)check(std::abs(axis[i]-identity[i])<1e-6,"Rotation did not return to baseline");
  // Controller basis and target handedness are inputs; no axis is mirrored
  // implicitly. Both modes use the same frame conversion.
  check(calibrateWeaponAttachment(from,turned,from,turned,origin,axis),"Neutral correction failed");
  check(std::abs(origin[0]-5)<1e-6&&std::abs(origin[1]-4)<1e-6,"Neutral correction changed the ordinary weapon");
  WeaponIdlePose idle;
  check(idle.apply(1,2,3,true,true,origin,axis,100),"Initial idle pose missing");
  check(!idle.apply(1,2,3,true,false,origin,axis,200),"Shared hands root replayed the previous weapon pose");
  check(idle.apply(1,2,3,true,true,origin,axis,200),"New weapon failed to establish its own pose");
 }
 {
  WeaponPoseCalibration p;p.mode=kharvox::hands::CalibrationMode::Position;
  p.draft.saved["default_right"].position.x=.02f;p.draft.saved["default_left"].position.x=-.03f;
  auto offset=[&](const char* name,bool left){WeaponCalibration c;p.apply(c,name,left);return c.pose.offset.x;};
  check(std::abs(offset("rocket_launcher",false)-.02f)<1e-6&&std::abs(offset("rocket_launcher",true)+.03f)<1e-6,"Handed default fallback failed");
  std::array<bool,6> step{};step[1]=true;p.step("rocket_launcher",false,step,false,false);
  check(std::abs(offset("rocket_launcher",false)-.025f)<1e-6,"First override did not start at visible default");
  p.draft.saved["default_right"].position.x=.1f;
  check(std::abs(offset("rocket_launcher",false)-.025f)<1e-6&&std::abs(offset("ballista",false)-.1f)<1e-6,"Default was added to override or failed for ordinary weapon");
  check(std::string(argent::eternalCalibrationProfile(KharvoxWeaponKind::Unknown,false,true))=="sentinel_hammer","Hammer profile mapping failed");
  p.draft.saved["sentinel_hammer_right"].position.x=.12f;
  p.draft.saved["sentinel_hammer_left"].position.x=-.09f;
  check(std::abs(offset("sentinel_hammer",false)-.12f)<1e-6&&std::abs(offset("sentinel_hammer",true)+.09f)<1e-6,"Hammer handedness leaked");
  p.draft.saved["crucible_right"].position.x=.07f;
  check(std::abs(offset("crucible",false)-.07f)<1e-6&&std::abs(offset("crucible",true)+.03f)<1e-6,"Crucible/hand override leaked");
  p.draft.saved["ballista_right"]={};check(offset("ballista",false)==0,"Explicit zero override inherited default");
  check(!p.canEdit("rocket_launcher","crucible")&&p.canEdit("default","crucible")&&p.canEdit("rocket_launcher","rocket_launcher"),"Calibration wrote wrong weapon target");
  for(auto pair:{std::pair{"weapon/player/rocket_launcher","rocket_launcher"},std::pair{"weapon/player/heavy_cannon","heavy_cannon"},std::pair{"weapon/player/shotgun","combat_shotgun"},std::pair{"weapon/player/double_barrel","super_shotgun"}})
   check(std::string(argent::eternalCalibrationProfile(argent::eternalHapticsWeapon(pair.first)))==pair.second,"Native weapon profile detection mismatch");
  check(std::string(argent::eternalCalibrationProfile(KharvoxWeaponKind::Unknown,true))=="crucible","Crucible profile detection failed");
 }
 const auto root=std::filesystem::temp_directory_path()/("ArgentWeaponCalibration-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));std::filesystem::create_directories(root);
 checkSupportPersistence(root);
 WeaponPoseCalibration editor;editor.load(root);
 {
  const auto folder=root/L"defaults";std::filesystem::create_directories(folder);
  {std::ofstream out(folder/L"weapon_pose_calibration_default.cfg");out<<"default_left 0.005 -0.010 0.060 0 -20 5\nrocket_launcher_right -0.010 0.055 0.145 0 0 0\n";}
  WeaponPoseCalibration shipped;shipped.load(folder);
  check(std::abs(shipped.resolved("super_shotgun",true).position.z-.060f)<1e-6,"Shipped left default not inherited");
  check(std::abs(shipped.resolved("rocket_launcher",false).position.z-.145f)<1e-6,"Shipped individual pose missing");
  check(shipped.resolved("super_shotgun",false).position.z==0,"Left default leaked to right mode");
  {std::ofstream out(folder/L"weapon_pose_calibration_saved.cfg");out<<"rocket_launcher_right 0 0 0.1 0 0 0\n";}
  WeaponPoseCalibration overridden;overridden.load(folder);
  check(std::abs(overridden.resolved("rocket_launcher",false).position.z-.1f)<1e-6,"User save did not override shipped default");
  check(std::abs(overridden.resolved("super_shotgun",true).position.z-.060f)<1e-6,"User save discarded unrelated default");
 }
 {
  const auto folder=root/L"apply";std::filesystem::create_directories(folder);
  WeaponPoseCalibration p;p.load(folder);p.mode=kharvox::hands::CalibrationMode::Position;
  p.configure("rocket_launcher",false,true,{});
  std::array<bool,6> change{};change[1]=true;p.step("rocket_launcher",false,change,false,false);
  check(p.draft.saved.empty()&&!std::filesystem::exists(folder/L"weapon_pose_calibration_saved.cfg"),"Preview was saved without Apply");
  check(p.resolved("rocket_launcher",false).position.x>.004f,"Preview missing");
  argent::calibration::ApplyCommand command{1,"weapon","rocket_launcher",false};
  p.configure("crucible",false,true,command); // Apply old target, then select next.
  WeaponPoseCalibration disk;disk.load(folder);
  check(disk.resolved("rocket_launcher",false).position.x>.004f&&disk.resolved("crucible",false).position.x==0,"Apply followed by selection saved wrong profile");
  p.step("crucible",false,change,false,false);p.configure("crucible",false,false,command);
  check(p.resolved("crucible",false).position.x==0,"Leaving mode did not discard preview");
  p.configure("crucible",true,true,command);p.step("crucible",true,change,false,false);
  command={2,"weapon","rocket_launcher",true};p.configure("crucible",true,true,command);
  check(!p.draft.pending.empty()&&!p.draft.saved.count("crucible_left"),"Wrong-target Apply committed preview");
  command={3,"weapon","crucible",true};p.configure("crucible",true,true,command);
  check(p.draft.pending.empty()&&p.draft.saved.count("crucible_left")&&!p.draft.saved.count("crucible_right"),"Apply leaked handedness");
  argent::calibration::Draft<float> hands;
  using namespace kharvox::hands;
  const auto a=handPoseKey(HandWeaponKind::Crucible,false,false),b=handPoseKey(HandWeaponKind::Crucible,true,false),c=handPoseKey(HandWeaponKind::Crucible,false,true);
  check(a!=b&&a!=c&&b!=c,"Physical hand and dominant mode conflated");
  hands.saved[b]=2;hands.select("crucible_right");hands.edit(a,0)=1;hands.edit(c,0)=3;
  check(!hands.apply([](const auto&){return false;})&&hands.saved.size()==1&&hands.pending.size()==2,"Failed save changed committed hands");
  check(hands.apply([](const auto&){return true;})&&hands.saved.at(a)==1&&hands.saved.at(b)==2&&hands.saved.at(c)==3,"Apply changed unrelated hand mode");
 }
 std::array<bool,6> keys{};keys[1]=true;
 check(editor.step("combat_shotgun",false,keys,false,false),"No rotation change");
 editor.mode=kharvox::hands::CalibrationMode::Position;
 check(editor.step("combat_shotgun",false,keys,false,false),"No position change");
 check(std::abs(editor.resolved("combat_shotgun",false).position.x-.005f)<1e-6,"Wrong KHARVOX coarse step");
 editor.step("combat_shotgun",false,keys,true,false);
 check(editor.save(),"Save failed");WeaponPoseCalibration restored;restored.load(root);
 WeaponCalibration base;base.pose.offset={.01f,.02f,.03f};auto right=base,left=base;
 restored.apply(right,"combat_shotgun",false);restored.apply(left,"combat_shotgun",true);
 check(std::abs(right.pose.offset.x-.016f)<1e-6&&left.pose.offset.x==base.pose.offset.x,"Hand profiles leaked or baseline replaced");
 check(std::abs(right.pose.rotation.x)>0.01f&&argent::camera::validQuaternion(right.pose.rotation),"Rotation missing");
 restored.step("combat_shotgun",false,{},false,true);right=base;restored.apply(right,"combat_shotgun",false);
 check(right.pose.offset.x==base.pose.offset.x&&right.pose.rotation.w==1,"Reset did not restore base");
 WeaponConfig config;std::istringstream valid("show_hands 1\ncalibration_mode hands\nprofile ballista\n");
 check(readWeaponConfig(valid,config)&&config.showHands&&config.calibrationMode=="hands"&&config.profile=="ballista","Calibration config not accepted");
 std::istringstream invalid("calibration_mode anything\n");check(!readWeaponConfig(invalid,config),"Unknown mode accepted");
 unsigned configCases{},configFailures{};
 const auto reject=[&](std::istream& stream,const char* name){
  auto candidate=config;++configCases;
  if(readWeaponConfig(stream,candidate)||candidate.profile!=config.profile||candidate.calibrationMode!=config.calibrationMode||candidate.leftHanded!=config.leftHanded){++configFailures;std::cerr<<name<<": invalid configuration published\n";}
 };
 for(const auto text:{"calibration_mode", "calibration_apply_profile", "calibration_apply_mode", "profile"}){std::istringstream row(text);reject(row,text);}
 for(const auto state:{std::ios::badbit,std::ios::failbit}){std::istringstream rows("dominant left\n");rows.setstate(state);reject(rows,state==std::ios::badbit?"bad stream":"failed stream");}
 FailedConfigurationBuffer failedBuffer("dominant left\n");std::istream failedInput(&failedBuffer);reject(failedInput,"failure after complete row");
 std::istringstream empty("");WeaponConfig defaults;++configCases;if(!readWeaponConfig(empty,defaults)||defaults.profile!="default")++configFailures;
 std::istringstream lastRow("calibration_mode hands");++configCases;if(!readWeaponConfig(lastRow,defaults)||defaults.calibrationMode!="hands")++configFailures;
 std::cout<<configCases<<" configuration boundary scenarios, "<<configFailures<<" failures\n";check(!configFailures,"Configuration boundaries failed");
 restored.mode=kharvox::hands::CalibrationMode::Position;
 restored.step("crucible",false,keys,false,false);restored.step("crucible",true,keys,true,false);
 check(restored.save(),"Crucible calibration save failed");WeaponPoseCalibration sword;sword.load(root);
 right=base;left=base;sword.apply(right,"crucible",false);sword.apply(left,"crucible",true);
 check(std::abs(right.pose.offset.x-.015f)<1e-6&&std::abs(left.pose.offset.x-.011f)<1e-6,"Crucible handed profiles did not persist separately");
 auto gun=base;sword.apply(gun,"combat_shotgun",false);check(gun.pose.offset.x==base.pose.offset.x,"Crucible calibration leaked into gun profile");
 std::cout<<"PASS: separate hand/profile deltas, saved reload, baseline reset and config validation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
