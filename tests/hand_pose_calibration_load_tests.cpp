#include <windows.h>
#include "../src/hands/HandRenderer.h"
#include "../src/hands/HandCalibrationPolicy.h"
#include "../src/hands/CalibrationDraft.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

static unsigned reads{},failReadAfter=std::numeric_limits<unsigned>::max();
static BOOL readCalibration(HANDLE handle,LPVOID buffer,DWORD size,LPDWORD count,LPOVERLAPPED overlapped) {
    if(reads++==failReadAfter){SetLastError(ERROR_READ_FAULT);return FALSE;}
    return ReadFile(handle,buffer,size,count,overlapped);
}
#define ReadFile readCalibration
#include "../src/openxr/ConfigurationFile.h"
#undef ReadFile
#include "../src/hands/CalibrationFile.h"

namespace kharvox::hands {
struct HandLoadFixture {
    struct Impl {
        std::filesystem::path root;
        argent::calibration::Draft<HandCalibration> calibrationDraft;
        HandCalibration leftCalibration{{.2f,.3f,.4f},{20,30,40},.7f};
        HandCalibration rightCalibration{{-.2f,-.3f,-.4f},{-20,-30,-40},.8f};
        std::array<std::array<HandCalibration,2>,size_t(HandWeaponKind::Count)> weaponCalibrations{};
        static std::string poseKey(HandWeaponKind weapon,bool modeLeft,bool physicalLeft){return handPoseKey(weapon,modeLeft,physicalLeft);}
        void loadPoseCalibrationFile(const wchar_t* filename);
        const HandCalibration& renderCalibration(bool left,HandModelKind kind,HandWeaponKind weapon,bool leftHanded)const;
    };
};
#define HandRenderer HandLoadFixture
#include "../src/hands/HandPoseCalibration.inc"
#undef HandRenderer
}

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool equal(const std::map<std::string,kharvox::hands::HandCalibration>& left,
    const std::map<std::string,kharvox::hands::HandCalibration>& right) {
    if(left.size()!=right.size())return false;
    for(const auto& [key,value]:left){
        const auto found=right.find(key);if(found==right.end()||value.scale!=found->second.scale)return false;
        for(size_t i=0;i<3;++i)if(value.position[i]!=found->second.position[i]
            ||value.rotationDegrees[i]!=found->second.rotationDegrees[i])return false;
    }
    return true;
}

int main(int argc,char** argv){try{
    check(argc==2,"Missing checked-in asset directory");
    using namespace kharvox::hands;
    struct Directory {
        const std::filesystem::path path=std::filesystem::temp_directory_path()
            /(L"argent-hand-load-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Directory(){std::filesystem::create_directory(path);}
        ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } directory;
    HandLoadFixture::Impl renderer;renderer.root=directory.path;
    const auto path=directory.path/L"hand_pose_calibration_default.cfg";
    const auto key=handPoseKey(HandWeaponKind::Unknown,true,true);
    const auto other=handPoseKey(HandWeaponKind::Unknown,false,false);
    unsigned cases{},failures{};
    const auto expect=[&](bool value,const char* name){++cases;if(!value){++failures;std::cerr<<name<<": hand load invariant failed\n";}};
    std::filesystem::copy_file(std::filesystem::path(argv[1])/L"hand_pose_calibration_default.cfg",path);
    renderer.loadPoseCalibrationFile(path.filename().c_str());
    const auto& inherited=renderer.renderCalibration(true,HandModelKind::GunHolding,HandWeaponKind::RocketLauncher,true);
    expect(renderer.calibrationDraft.saved.count(key)&&std::abs(inherited.position[1]+.045f)<1e-6f
        &&std::abs(inherited.scale-.2464f)<1e-6f,"checked-in commented left-handed default");
    expect(renderer.renderCalibration(true,HandModelKind::GunHolding,HandWeaponKind::RocketLauncher,false).scale==.7f,
        "dominant-mode isolation");

    const std::map<std::string,HandCalibration> accepted{{key,renderer.leftCalibration},{other,renderer.rightCalibration}};
    renderer.calibrationDraft.scope="rocket_launcher_left";
    renderer.calibrationDraft.pending.emplace("preview",renderer.leftCalibration);
    const auto pending=renderer.calibrationDraft.pending;
    const auto write=[&](const std::string& bytes){
        std::ofstream output(path,std::ios::binary);output<<bytes;output.close();check(bool(output),"Cannot prepare hand load fixture");
        renderer.calibrationDraft.saved=accepted;reads=0;
    };
    const auto valid=key+" .1 .2 .3 10 20 30 .4";
    write("  # header\r\n\r\n"+valid+" # inline note\r\n");renderer.loadPoseCalibrationFile(path.filename().c_str());
    expect(renderer.calibrationDraft.saved.at(key).scale==.4f&&renderer.calibrationDraft.saved.at(other).scale==.8f,
        "comments, CRLF and unrelated profiles");
    write(valid);renderer.loadPoseCalibrationFile(path.filename().c_str());
    expect(renderer.calibrationDraft.saved.at(key).scale==.4f,"final row without newline");
    write(valid+"\n"+key+" .1 .2 .3 10 20 30 .5\n");renderer.loadPoseCalibrationFile(path.filename().c_str());
    expect(renderer.calibrationDraft.saved.at(key).scale==.5f,"last duplicate wins");
    write("future_profile_mode_left_hand_right 0 0 0 0 0 0 .3\n");renderer.loadPoseCalibrationFile(path.filename().c_str());
    expect(renderer.calibrationDraft.saved.size()==3,"unknown complete profile retained");
    const auto reject=[&](const char* name){renderer.loadPoseCalibrationFile(path.filename().c_str());expect(equal(renderer.calibrationDraft.saved,accepted),name);};
    for(const auto row:{"truncated 0 0", "invalid 0 0 0 0 0 0 0", "invalid 0 0 0 0 0 0 -.4",
        "invalid nan 0 0 0 0 0 .4", "invalid 0 0 0 inf 0 0 .4", "invalid 0 0 0 0 0 0 nan"}){
        write(valid+"\n"+row+"\n");reject("malformed/invalid suffix retains full baseline");
    }
    write(valid+" extra\n");reject("unexpected trailing tokens");
    write("# only comments\n\n");reject("comment-only overlay");
    write("");reject("empty overlay");
    std::filesystem::remove(path);renderer.calibrationDraft.saved=accepted;reject("missing overlay");
    write(valid+"\n");
    {
        const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(handle!=INVALID_HANDLE_VALUE,"Cannot open native hand writer");
        const std::unique_ptr<void,decltype(&CloseHandle)> writer(handle,&CloseHandle);
        reject("active writer refused");
    }
    write(valid+"\n"+std::string(9000,'#')+"\n");failReadAfter=1;reject("native read error after prefix");
    failReadAfter=std::numeric_limits<unsigned>::max();
    write(valid+"\n");renderer.loadPoseCalibrationFile(path.filename().c_str());
    expect(renderer.calibrationDraft.saved.at(key).scale==.4f,"retry after failure");
    expect(equal(renderer.calibrationDraft.pending,pending)&&renderer.calibrationDraft.scope=="rocket_launcher_left",
        "preview and scope preservation");
    auto candidate=accepted;
    expect(!argent::calibration::readCalibrationFile(path,candidate,[](auto&,auto&)->bool{throw std::bad_alloc();})
        &&equal(candidate,accepted),"shared decoder allocation failure");
    expect(!argent::calibration::readCalibrationFile(path,candidate,[](auto& row,auto&){row.setstate(std::ios::badbit);return true;})
        &&equal(candidate,accepted),"shared row failure refusal");
    std::cout<<cases<<" production hand-pose load/resolution scenarios, "<<failures<<" failures\n";
    check(!failures,"Hand pose calibration load failed");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
