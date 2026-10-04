#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

static unsigned reads{},failReadAfter=std::numeric_limits<unsigned>::max();
static BOOL readCalibration(HANDLE handle,LPVOID buffer,DWORD size,LPDWORD count,LPOVERLAPPED overlapped){
    if(reads++==failReadAfter){SetLastError(ERROR_READ_FAULT);return FALSE;}
    return ReadFile(handle,buffer,size,count,overlapped);
}
#define ReadFile readCalibration
#include "../src/openxr/ConfigurationFile.h"
#undef ReadFile
#include "../src/hands/WeaponPoseCalibration.h"

using Calibration=argent::input::WeaponPoseCalibration;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool equal(const std::map<std::string,Calibration::Delta>& left,
    const std::map<std::string,Calibration::Delta>& right){
    if(left.size()!=right.size())return false;
    for(const auto& [key,value]:left){
        const auto found=right.find(key);if(found==right.end())return false;
        const auto& other=found->second;
        if(value.position.x!=other.position.x||value.position.y!=other.position.y||value.position.z!=other.position.z
            ||value.degrees.x!=other.degrees.x||value.degrees.y!=other.degrees.y||value.degrees.z!=other.degrees.z)return false;
    }
    return true;
}

int main(int argc,char** argv){try{
    check(argc==2,"Missing checked-in asset directory");
    struct Directory{
        const std::filesystem::path path=std::filesystem::temp_directory_path()
            /(L"argent-weapon-load-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Directory(){std::filesystem::create_directory(path);}
        ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } directory;
    const auto defaults=directory.path/L"weapon_pose_calibration_default.cfg";
    const auto saved=directory.path/L"weapon_pose_calibration_saved.cfg";
    unsigned cases{},failures{};
    const auto expect=[&](bool value,const char* name){++cases;if(!value){++failures;std::cerr<<name<<": weapon load invariant failed\n";}};
    const auto write=[](const auto& path,const std::string& bytes){
        std::ofstream output(path,std::ios::binary);output<<bytes;output.close();check(bool(output),"Cannot prepare weapon load fixture");
    };
    std::filesystem::copy_file(std::filesystem::path(argv[1])/L"weapon_pose_calibration_default.cfg",defaults);
    Calibration shipped;shipped.load(directory.path);
    expect(shipped.draft.saved.size()==9&&std::abs(shipped.resolved("rocket_launcher",false).position.z-.145f)<1e-6f,
        "checked-in weapon profiles");
    expect(std::abs(shipped.resolved("ballista",true).position.z-.060f)<1e-6f
        &&shipped.resolved("ballista",false).position.z==0,"handed default inheritance");
    const std::string defaultRows="  # header\r\ndefault_left .005 -.010 .060 0 -20 5 # note\r\nrocket_launcher_right -.010 .055 .145 0 0 0\r\n";
    write(defaults,defaultRows);
    Calibration baseline;baseline.load(directory.path);
    const auto accepted=baseline.draft.saved;
    expect(accepted.size()==2,"comments and CRLF");
    const std::string prefix="rocket_launcher_right .1 .2 .3 10 20 30";
    const auto prepare=[&](const std::string& bytes){write(saved,bytes);reads=0;};
    const auto reject=[&](const char* name){Calibration editor;editor.load(directory.path);expect(equal(editor.draft.saved,accepted),name);};
    for(const auto row:{"truncated 0 0", "invalid .51 .5 .5 0 0 0", "invalid 0 0 0 181 0 0",
        "invalid nan 0 0 0 0 0", "invalid 0 0 0 inf 0 0", "invalid 0 0 0 0 0 1e100"}){
        prepare(prefix+"\n"+row+"\n");reject("malformed/invalid suffix retains complete defaults");
    }
    prepare("truncated 0 0\n"+prefix+"\n");reject("malformed prefix refuses later valid row");
    prepare(prefix+" extra\n");reject("unexpected trailing tokens");
    prepare(prefix+"oops\n");reject("numeric suffix refused");
    prepare(prefix+" # note\r\nfuture_profile_left 0 0 0 0 0 0\r\n");
    Calibration overlay;overlay.load(directory.path);
    expect(overlay.draft.saved.size()==3&&overlay.resolved("rocket_launcher",false).position.z==.3f
        &&std::abs(overlay.resolved("ballista",true).position.z-.060f)<1e-6f,"complete saved overlay and unknown profile");
    prepare(prefix);Calibration lastRow;lastRow.load(directory.path);
    expect(lastRow.resolved("rocket_launcher",false).position.z==.3f,"final row without newline");
    prepare(prefix+"\nrocket_launcher_right 0 0 .4 0 0 0\n");Calibration duplicate;duplicate.load(directory.path);
    expect(duplicate.resolved("rocket_launcher",false).position.z==.4f,"last duplicate wins");
    prepare("boundary_right .5 .5 .5 -180 180 180\n");Calibration boundary;boundary.load(directory.path);
    expect(boundary.draft.saved.count("boundary_right")&&boundary.resolved("boundary",false).degrees.y==180,
        "existing position and rotation limits preserved");
    prepare("");reject("empty saved overlay");
    prepare("# only comments\n\n");reject("comment-only saved overlay");
    std::filesystem::remove(saved);reads=0;reject("missing saved overlay");
    prepare(prefix+"\n");
    {
        const auto handle=CreateFileW(saved.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(handle!=INVALID_HANDLE_VALUE,"Cannot open native weapon writer");
        const std::unique_ptr<void,decltype(&CloseHandle)> writer(handle,&CloseHandle);
        reject("active saved writer refused");
    }
    prepare(prefix+"\n"+std::string(9000,'#')+"\n");failReadAfter=3;reject("native saved read error after prefix");
    failReadAfter=std::numeric_limits<unsigned>::max();
    prepare(prefix+"\n");Calibration retry;retry.load(directory.path);
    expect(retry.resolved("rocket_launcher",false).position.z==.3f,"fresh editor retry after read failure");
    const auto before=retry.draft.saved;const auto count=reads;
    write(saved,"rocket_launcher_right 0 0 .4 0 0 0\n");retry.load(directory.path/L"unused");
    expect(retry.loaded&&retry.root==directory.path&&reads==count&&equal(retry.draft.saved,before),
        "one-time load performs no per-frame reread");
    std::filesystem::remove(saved);
    const std::map<std::string,Calibration::Delta> prior{{"ballista_right",{{.1f,0,0},{0,0,0}}}};
    write(defaults,prefix+"\ntruncated 0 0\n");Calibration invalidDefault;invalidDefault.draft.saved=prior;
    invalidDefault.load(directory.path);
    expect(equal(invalidDefault.draft.saved,prior),"invalid default retains prior committed map");
    write(defaults,defaultRows);Calibration preview;preview.draft.scope="rocket_launcher_right";
    preview.draft.pending.emplace("rocket_launcher_right",Calibration::Delta{{.2f,0,0},{0,0,0}});
    const auto pending=preview.draft.pending;preview.load(directory.path);
    expect(equal(preview.draft.pending,pending)&&preview.draft.scope=="rocket_launcher_right"
        &&preview.resolved("rocket_launcher",false).position.x==.2f,"pending preview and scope retained");
    std::cout<<cases<<" production weapon-pose load/resolution scenarios, "<<failures<<" failures\n";
    check(!failures,"Weapon pose calibration load failed");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
