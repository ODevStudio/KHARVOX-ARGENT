#include <windows.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

static DWORD run(const std::filesystem::path& executable,const std::filesystem::path& fixture,const wchar_t* mode){
    std::wstring command=L"\""+executable.wstring()+L"\" \""+fixture.wstring()+L"\" "+mode;
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot start hand retirement check");
    const auto wait=WaitForSingleObject(process.hProcess,30000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,88);WaitForSingleObject(process.hProcess,5000);}
    DWORD code{};const bool exited=GetExitCodeProcess(process.hProcess,&code)!=FALSE;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(wait==WAIT_OBJECT_0,"Hand retirement check timed out");
    check(exited,"Cannot read hand retirement result");
    return code;
}

int main(int argc,char** argv){try{
    check(argc==3,"Missing hand renderer executable or fixture");
    const auto executable=std::filesystem::absolute(argv[1]);
    const auto fixture=std::filesystem::absolute(argv[2]);
    struct Scenario {const wchar_t* mode;DWORD expected;};
    const std::array<Scenario,5> scenarios{{
        {L"--fail-upload-retirement",0xc0000602u},
        {L"--fail-shutdown-retirement",0xc0000602u},
        {L"--fail-reset",0},
        {L"--lost-upload",0},
        {L"--lost-shutdown",0}
    }};
    for(const auto& scenario:scenarios){
        const auto result=run(executable,fixture,scenario.mode);
        std::wcout<<scenario.mode<<L" exit=0x"<<std::hex<<result<<std::dec<<L'\n';
        check(result==scenario.expected,"Hand renderer freed unretired resources or continued after failed command reset");
    }
    std::cout<<"Hand upload/shutdown retirement, reset failure and device-loss cleanup passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
