#include <windows.h>
#include "../src/hands/HandRenderer.h"
#include "../src/hands/CalibrationDraft.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

static unsigned reads{},failReadAfter=std::numeric_limits<unsigned>::max();
static BOOL readCalibration(HANDLE handle,LPVOID buffer,DWORD size,LPDWORD count,LPOVERLAPPED overlapped){
    if(reads++==failReadAfter){SetLastError(ERROR_READ_FAULT);return FALSE;}
    return ReadFile(handle,buffer,size,count,overlapped);
}
#define ReadFile readCalibration
#include "../src/openxr/ConfigurationFile.h"
#undef ReadFile
#include "../src/hands/HandConfigurationFile.h"
namespace kharvox::hands {
struct LegacyLoadFixture {
    struct Impl {
        std::filesystem::path root;
        HandCalibration leftCalibration{{.2f,.3f,.4f},{20,30,40},.7f};
        HandCalibration rightCalibration{{-.2f,-.3f,-.4f},{-20,-30,-40},.8f};
        std::array<std::array<HandCalibration,2>,size_t(HandWeaponKind::Count)> weaponCalibrations{};
        argent::calibration::Draft<HandCalibration> calibrationDraft;
        bool failLog{};
        void say(const std::string&)const{if(failLog)throw std::bad_alloc();}
        void loadCalibrationFile(const wchar_t* filename,bool defaults);
    };
};
#define HandRenderer LegacyLoadFixture
#include "../src/hands/HandLegacyCalibration.inc"
#undef HandRenderer
}

static bool equal(const kharvox::hands::HandCalibration& left,const kharvox::hands::HandCalibration& right){
    if(left.scale!=right.scale)return false;
    for(size_t i=0;i<3;++i)if(left.position[i]!=right.position[i]||left.rotationDegrees[i]!=right.rotationDegrees[i])return false;
    return true;
}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    check(argc==2,"Missing checked-in asset directory");
    using namespace kharvox::hands;
    struct Directory {
        const std::filesystem::path path=std::filesystem::temp_directory_path()
            /(L"argent-legacy-load-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Directory(){std::filesystem::create_directory(path);}
        ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } directory;
    const auto path=directory.path/L"hand_models_calibration_saved.cfg";
    LegacyLoadFixture::Impl initial;initial.root=directory.path;
    for(auto& profile:initial.weaponCalibrations){profile[0]=initial.rightCalibration;profile[1]=initial.leftCalibration;}
    initial.calibrationDraft.scope="preview";initial.calibrationDraft.pending.emplace("preview",initial.leftCalibration);
    unsigned cases{},failures{};
    const auto expect=[&](bool value,const char* name){++cases;if(!value){++failures;std::cerr<<name<<": legacy load invariant failed\n";}};
    const auto same=[&](const auto& value){
        if(!equal(value.leftCalibration,initial.leftCalibration)||!equal(value.rightCalibration,initial.rightCalibration))return false;
        for(size_t i=0;i<initial.weaponCalibrations.size();++i)for(size_t hand=0;hand<2;++hand)
            if(!equal(value.weaponCalibrations[i][hand],initial.weaponCalibrations[i][hand]))return false;
        return true;
    };
    const auto write=[&](const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();check(bool(out),"Cannot prepare legacy fixture");reads=0;};
    const auto load=[&](auto& value,bool defaults=false){bool escaped=false;try{value.loadCalibrationFile(path.filename().c_str(),defaults);}catch(...){escaped=true;}return !escaped;};
    const auto reject=[&](const char* name,bool defaults=false){auto value=initial;expect(load(value,defaults)&&same(value),name);};
    std::filesystem::copy_file(std::filesystem::path(argv[1])/L"hand_models_calibration_default.cfg",path);
    auto shipped=initial;load(shipped,true);
    expect(std::abs(shipped.leftCalibration.position[0]+.005f)<1e-6f&&shipped.rightCalibration.rotationDegrees[0]==90,
        "checked-in global wrists");
    expect(equal(shipped.weaponCalibrations[size_t(HandWeaponKind::RocketLauncher)][0],shipped.leftCalibration)
        &&shipped.leftCalibration.scale==.7f&&shipped.rightCalibration.scale==.8f,"default weapon inheritance and model scales");
    write("version=2\nrocket_launcher_left_position=.1 .2 .3\nfuture_setting=preserved\n");auto overlay=initial;load(overlay);
    expect(overlay.weaponCalibrations[size_t(HandWeaponKind::RocketLauncher)][0].position[0]==.1f
        &&equal(overlay.weaponCalibrations[size_t(HandWeaponKind::Ballista)][0],initial.rightCalibration),"v2 partial profile overlay");
    for(const auto version:{"version=1\n",""}){
        write(std::string(version)+"left_position=.1 .2 .3\nrocket_launcher_right_position=.4 .5 .6\n");auto legacy=initial;load(legacy);
        expect(legacy.weaponCalibrations[size_t(HandWeaponKind::Ballista)][0].position[0]==.1f
            &&legacy.weaponCalibrations[size_t(HandWeaponKind::RocketLauncher)][1].position[0]==.4f,"v1 global reset then named overrides");
    }
    const std::string prefix="version=2\nleft_position=.1 .2 .3\n";
    for(const auto row:{"right_rotation=1 2", "right_position=nan 0 0", "rocket_launcher_left_position=0 0 inf",
        "rocket_launcher_right_rotation=1e100 0 0", "left_rotation=1 2 3 extra", "left_rotation=1 2 3oops",
        "left_rotation=", "invalid row without separator", "=0 0 0"}){
        write(prefix+row+"\n");reject("malformed/invalid suffix retains all wrists and profiles");
    }
    write("left_rotation=1 2\n"+prefix);reject("malformed prefix refuses later valid fields");
    write(prefix+"version=unsupported\n");reject("unknown calibration version refused");
    write(prefix+"right_rotation=1 2\n");reject("invalid defaults retain full baseline",true);
    write("  # header\r\n version = 2 \r\n left_position = .1 .2 .3 # note\r\n");auto comments=initial;load(comments);
    expect(comments.leftCalibration.position[0]==.1f,"comments and CRLF");
    write(prefix+"left_position=.4 .5 .6");auto duplicate=initial;load(duplicate);
    expect(duplicate.leftCalibration.position[0]==.4f,"last duplicate and final row without newline");
    write("version=2\n");reject("empty v2 overlay");
    std::filesystem::remove(path);reject("missing saved overlay");
    auto missing=initial;missing.failLog=true;const bool contained=load(missing,true);
    expect(contained&&equal(missing.weaponCalibrations[size_t(HandWeaponKind::Ballista)][0],missing.leftCalibration),
        "missing default fallback survives diagnostic failure");
    write(prefix);
    {
        const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(handle!=INVALID_HANDLE_VALUE,"Cannot open native legacy writer");
        const std::unique_ptr<void,decltype(&CloseHandle)> writer(handle,&CloseHandle);reject("active writer refused");
    }
    write(prefix+std::string(9000,'#')+"\n");failReadAfter=1;reject("native read error after valid prefix");
    failReadAfter=std::numeric_limits<unsigned>::max();
    write(prefix);auto retry=initial;expect(load(retry)&&retry.leftCalibration.position[0]==.1f,"retry after failure");
    auto diagnostics=initial;diagnostics.failLog=true;
    expect(load(diagnostics)&&diagnostics.leftCalibration.position[0]==.1f,"publication survives diagnostic failure");
    expect(diagnostics.calibrationDraft.scope=="preview"&&diagnostics.calibrationDraft.pending.size()==1
        &&equal(diagnostics.calibrationDraft.pending.at("preview"),initial.leftCalibration),"pending preview and scope retained");
    std::cout<<cases<<" production legacy hand-calibration load scenarios, "<<failures<<" failures\n";
    check(!failures,"Legacy hand calibration load failed");return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
