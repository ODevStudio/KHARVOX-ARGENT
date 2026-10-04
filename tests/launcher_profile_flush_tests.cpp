#include <windows.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

static void check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}

static std::wstring readIni(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    check(bool(input),"Cannot read native INI snapshot");
    const std::string bytes(std::istreambuf_iterator<char>(input),{});
    check(bytes.size()%sizeof(wchar_t)==0,"INI snapshot is not complete UTF-16");
    std::wstring result(bytes.size()/sizeof(wchar_t),L'\0');
    std::memcpy(result.data(),bytes.data(),bytes.size());
    return result;
}

int main() { try {
    struct Directory {
        const std::filesystem::path path=std::filesystem::temp_directory_path()
            /(L"argent-profile-flush-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Directory(){std::filesystem::create_directory(path);}
        ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
    } directory;
    const auto target=directory.path/L"settings.ini";
    const auto temporary=directory.path/L"settings.ini.tmp";
    {
        std::ofstream output(target,std::ios::binary);
        output.write("\xff\xfe",2);output.close();
        check(bool(output),"Cannot create UTF-16 INI");
    }
    check(WritePrivateProfileStringW(L"unknown",L"preserved",L"yes",target.c_str())!=FALSE,"Native initial key write failed");
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,target.c_str());
    std::filesystem::copy_file(target,temporary);
    check(WritePrivateProfileStringW(L"controls",L"calibration_mode",L"hands",temporary.c_str())!=FALSE,"Native temporary key write failed");
    SetLastError(ERROR_CANCELLED);
    const auto flush=WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    const auto error=GetLastError();
    const auto written=readIni(temporary);
    check(flush==FALSE,"Native all-null flush no longer returns zero");
    check(written.find(L"preserved=yes")!=std::wstring::npos
        &&written.find(L"calibration_mode=hands")!=std::wstring::npos,"Flush did not preserve complete disk contents");
    check(MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Native snapshot replacement failed");
    check(readIni(target)==written&&!std::filesystem::exists(temporary),"Replacement did not publish the flushed snapshot");
    wchar_t value[32]{};
    check(GetPrivateProfileStringW(L"controls",L"calibration_mode",L"",value,32,target.c_str())==5
        &&std::wstring(value)==L"hands","Replaced snapshot did not reload");
    const auto unavailable=directory.path/L"missing/settings.ini.tmp";
    check(WritePrivateProfileStringW(L"controls",L"calibration_mode",L"hands",unavailable.c_str())==FALSE,
        "Unavailable parent was not rejected by a normal key write");
    std::cout<<"Native INI flush returned "<<flush<<", last error "<<error
        <<"; disk contents, replacement, unknown-key preservation, reload and failed key write passed\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
