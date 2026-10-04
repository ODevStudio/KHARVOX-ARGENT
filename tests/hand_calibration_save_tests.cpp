#include "../src/hands/HandRenderer.h"
#include "../src/hands/CalibrationDraft.h"
#include <windows.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>

enum class SaveFault { None, Write, Flush, Close };
static SaveFault saveFault{};
static unsigned closes{}, replacements{};

namespace hand_save_stream {
using std::fixed;
using std::setprecision;
class ofstream : public std::ofstream {
public:
    explicit ofstream(const std::filesystem::path& path) : std::ofstream(path) {
        if(saveFault==SaveFault::Write)setstate(std::ios::badbit);
    }
    void flush() {
        std::ofstream::flush();
        if(saveFault==SaveFault::Flush)setstate(std::ios::badbit);
    }
    void close() {
        std::ofstream::close();
        ++closes;
        if(saveFault==SaveFault::Close)setstate(std::ios::failbit);
    }
};
}

static BOOL replaceSavedFile(LPCWSTR source,LPCWSTR target,DWORD flags) {
    ++replacements;
    return MoveFileExW(source,target,flags);
}

namespace kharvox::hands {
struct HandSaveFixture {
    struct Impl {
        std::filesystem::path root;
        argent::calibration::Draft<HandCalibration> calibrationDraft;
        bool saveCalibration();
    };
};
#define HandRenderer HandSaveFixture
#define std hand_save_stream
#define MoveFileExW replaceSavedFile
#include "../src/hands/HandCalibrationSave.inc"
#undef MoveFileExW
#undef std
#undef HandRenderer
}

static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}

static bool equal(const std::map<std::string,kharvox::hands::HandCalibration>& left,
    const std::map<std::string,kharvox::hands::HandCalibration>& right) {
    if(left.size()!=right.size())return false;
    for(const auto& [key,value]:left) {
        const auto found=right.find(key);
        if(found==right.end()||std::abs(value.scale-found->second.scale)>1e-6f)return false;
        for(size_t i=0;i<3;++i)
            if(std::abs(value.position[i]-found->second.position[i])>1e-6f
                ||std::abs(value.rotationDegrees[i]-found->second.rotationDegrees[i])>1e-6f)return false;
    }
    return true;
}

int main() { try {
    using namespace kharvox::hands;
    struct Directory {
        const std::filesystem::path path=std::filesystem::temp_directory_path()
            /(L"argent-hand-save-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Directory(){std::filesystem::create_directory(path);}
        ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } directory;
    HandSaveFixture::Impl renderer{directory.path};
    const auto target=directory.path/L"hand_pose_calibration_saved.cfg";
    const auto temporary=directory.path/L"hand_pose_calibration_saved.tmp";
    const auto bytes=[&] {
        std::ifstream in(target,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in),{});
    };
    unsigned cases{},failures{};
    const auto expect=[&](bool passed,const char* name) {
        ++cases;
        if(!passed){++failures;std::cerr<<name<<": hand save invariant failed\n";}
    };
    renderer.calibrationDraft.saved.emplace("rocket_launcher_left_left",HandCalibration{{.1f,.2f,.3f},{10,20,30},.4f});
    renderer.calibrationDraft.pending.emplace("ballista_right_right",HandCalibration{{-.1f,-.2f,-.3f},{-10,-20,-30},.5f});
    expect(renderer.saveCalibration()&&renderer.calibrationDraft.pending.empty()
        &&renderer.calibrationDraft.saved.size()==2&&closes==1&&replacements==1
        &&!std::filesystem::exists(temporary),"initial save");

    std::ifstream input(target);
    std::map<std::string,HandCalibration> disk;
    std::string key;HandCalibration value;
    while(input>>key>>value.position[0]>>value.position[1]>>value.position[2]
        >>value.rotationDegrees[0]>>value.rotationDegrees[1]>>value.rotationDegrees[2]>>value.scale)
        disk.emplace(key,value);
    expect(input.eof()&&equal(disk,renderer.calibrationDraft.saved),"complete round trip");
    input.close();

    const auto committed=renderer.calibrationDraft.saved;
    const auto original=bytes();
    const std::map<std::string,HandCalibration> pending{{"ballista_right_right",{{.2f,.3f,.4f},{40,50,60},.6f}}};
    const auto reject=[&](const char* name,unsigned expectedReplacements) {
        if(bytes()!=original){std::ofstream restore(target,std::ios::binary);restore<<original;restore.close();check(bool(restore),"Cannot restore isolated fixture");}
        renderer.calibrationDraft.saved=committed;
        renderer.calibrationDraft.pending=pending;
        closes=replacements=0;
        const bool saved=renderer.saveCalibration();
        expect(!saved&&equal(renderer.calibrationDraft.saved,committed)
            &&equal(renderer.calibrationDraft.pending,pending)&&bytes()==original
            &&closes==1&&replacements==expectedReplacements,name);
    };
    saveFault=SaveFault::Close;reject("close-only failure",0);
    saveFault=SaveFault::Flush;reject("flush failure",0);
    saveFault=SaveFault::Write;reject("write failure",0);
    saveFault=SaveFault::None;
    const auto locked=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
        OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(locked!=INVALID_HANDLE_VALUE,"Cannot lock hand calibration target");
    reject("replacement failure",1);
    CloseHandle(locked);
    std::filesystem::remove(temporary);
    std::filesystem::create_directory(temporary);
    reject("temporary open failure",0);
    std::filesystem::remove(temporary);
    closes=replacements=0;
    expect(renderer.saveCalibration()&&renderer.calibrationDraft.pending.empty()
        &&renderer.calibrationDraft.saved.at("ballista_right_right").scale==.6f
        &&equal({{"rocket_launcher_left_left",renderer.calibrationDraft.saved.at("rocket_launcher_left_left")}},
            {{"rocket_launcher_left_left",committed.at("rocket_launcher_left_left")}})
        &&closes==1&&replacements==1&&bytes()!=original,"successful retry");
    std::cout<<cases<<" production hand-save scenarios, "<<failures<<" failures\n";
    check(!failures,"Hand calibration persistence failed");
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
