#include <windows.h>
#include "../src/LauncherSettings.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

static unsigned reads{},failReadAfter=std::numeric_limits<unsigned>::max();
static BOOL readControls(HANDLE handle,LPVOID buffer,DWORD size,LPDWORD count,LPOVERLAPPED overlapped) {
    if(reads++==failReadAfter){SetLastError(ERROR_READ_FAULT);return FALSE;}
    return ReadFile(handle,buffer,size,count,overlapped);
}
#define ReadFile readControls
#include "../src/openxr/ConfigurationFile.h"
#undef ReadFile

static std::filesystem::path root;
static bool settingsFailed{};
static unsigned settingsSaves{},replacements{};
static void saveSettings() {
    ++settingsSaves;
    if(settingsFailed)throw std::runtime_error("Settings save rejected");
}
static std::map<std::string,std::string> controlsSettings() {
    return {{"turn","snap 45"},{"show_hands","1"}};
}
static BOOL replaceControls(LPCWSTR source,LPCWSTR target,DWORD flags) {
    ++replacements;
    return MoveFileExW(source,target,flags);
}
#define MoveFileExW replaceControls
#include "../src/LauncherControlsSave.inc"
#undef MoveFileExW

class FailedInput : public std::streambuf {
    std::string bytes;
public:
    explicit FailedInput(std::string input):bytes(std::move(input)){setg(bytes.data(),bytes.data(),bytes.data()+bytes.size());}
    int_type underflow()override{throw std::ios_base::failure("Interrupted controls read");}
};
static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}

int main() { try {
    struct Directory {
        const std::filesystem::path path=std::filesystem::temp_directory_path()
            /(L"argent-controls-save-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Directory(){std::filesystem::create_directory(path);}
        ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } directory;
    root=directory.path;
    std::filesystem::create_directory(root/L"assets");
    const auto target=root/L"assets/argent_controls.cfg";
    const auto temporary=root/L"assets/argent_controls.cfg.tmp";
    const std::string original="# retained comment\r\nhand future_weapon 0.1 0 0 0 0 0\r\nfuture_setting custom\r\nturn smooth\r\nturn snap 90\r\n";
    const auto write=[&](const std::string& bytes) {
        std::ofstream output(target,std::ios::binary);output<<bytes;output.close();
        check(bool(output),"Cannot prepare controls fixture");
        reads=replacements=settingsSaves=0;
    };
    const auto contents=[&] {
        std::ifstream input(target,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input),{});
    };
    unsigned cases{},failures{};
    const auto expect=[&](bool value,const char* name) {
        ++cases;
        if(!value){++failures;std::cerr<<name<<": controls persistence invariant failed\n";}
    };
    write(original);writeControlsConfig();
    const auto saved=contents();
    const auto turn=saved.find("turn snap 45");
    expect(saved.find("# retained comment")!=std::string::npos
        &&saved.find("hand future_weapon 0.1 0 0 0 0 0")!=std::string::npos
        &&saved.find("future_setting custom")!=std::string::npos
        &&turn!=std::string::npos&&saved.find("turn ",turn+1)==std::string::npos
        &&saved.find("\r\r\n")==std::string::npos&&settingsSaves==1&&replacements==1,"normal rewrite and CRLF");
    std::filesystem::remove(target);writeControlsConfig();
    expect(contents().find("show_hands 1")!=std::string::npos,"missing controls file");
    write("");writeControlsConfig();
    expect(contents().find("turn snap 45")!=std::string::npos,"empty controls file");

    const auto reject=[&](const std::string& before,const char* name,unsigned expectedReplacements) {
        bool rejected=false;
        try{writeControlsConfig();}catch(const std::exception&){rejected=true;}
        expect(rejected&&contents()==before&&replacements==expectedReplacements&&settingsSaves==1,name);
    };
    const auto lock=[&](DWORD access,DWORD share) {
        const auto handle=CreateFileW(target.c_str(),access,share,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(handle!=INVALID_HANDLE_VALUE,"Cannot lock controls fixture");
        return std::unique_ptr<void,decltype(&CloseHandle)>(handle,&CloseHandle);
    };
    write(original);
    {
        const auto handle=lock(GENERIC_READ,FILE_SHARE_DELETE);
        bool rejected=false;
        try{writeControlsConfig();}catch(const std::exception&){rejected=true;}
        expect(rejected&&replacements==0&&settingsSaves==1,"unreadable native input");
    }
    expect(contents()==original,"unreadable controls preservation");
    write(original);
    {const auto handle=lock(GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE);reject(original,"active native writer",0);}
    write(original);
    {const auto handle=lock(GENERIC_READ,FILE_SHARE_READ);reject(original,"replacement denied",1);}
    std::filesystem::remove(temporary);
    std::filesystem::create_directory(temporary);
    write(original);reject(original,"temporary open failure",0);
    std::filesystem::remove(temporary);
    write(original);settingsFailed=true;reject(original,"upstream settings failure",0);settingsFailed=false;
    const auto large=original+std::string(9000,'#')+"\r\nfuture_tail retained\r\n";
    write(large);failReadAfter=1;reject(large,"native read fault after prefix",0);
    failReadAfter=std::numeric_limits<unsigned>::max();
    reads=replacements=settingsSaves=0;writeControlsConfig();
    expect(contents().find("future_tail retained")!=std::string::npos&&replacements==1,"successful retry");

    const auto rejectStream=[&](std::istream& input,const char* name) {
        bool rejected=false;
        try{argent::launcher::updateSettings(input,controlsSettings());}catch(const std::exception&){rejected=true;}
        expect(rejected,name);
    };
    FailedInput interrupted("hand future_weapon 0.1 0 0 0 0 0\n");std::istream input(&interrupted);
    rejectStream(input,"interrupted formatter input");
    for(const auto state:{std::ios::badbit,std::ios::failbit,std::ios::eofbit}) {
        std::istringstream invalid("future_setting retained\n");invalid.setstate(state);
        rejectStream(invalid,"initially failed formatter input");
    }
    std::istringstream empty("");
    expect(argent::launcher::updateSettings(empty,controlsSettings()).find("turn snap 45")!=std::string::npos,"fresh empty formatter input");
    std::istringstream last("future_setting retained");
    expect(argent::launcher::updateSettings(last,controlsSettings()).find("future_setting retained\n")!=std::string::npos,"final row without newline");
    std::cout<<cases<<" production controls-save/formatter scenarios, "<<failures<<" failures\n";
    check(!failures,"Launcher controls persistence failed");
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
