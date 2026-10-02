#include "../src/LauncherProbeOutput.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static int child(const char* mode){
    const auto pipe=GetStdHandle(STD_OUTPUT_HANDLE);
    if(!std::strcmp(mode,"silent")){Sleep(INFINITE);return 0;}
    if(!std::strcmp(mode,"closed")){CloseHandle(pipe);Sleep(INFINITE);return 0;}
    const std::array<char,4096> bytes=[] {std::array<char,4096> value{};value.fill('x');return value;}();
    const unsigned blocks=!std::strcmp(mode,"flood")?1024:20;
    for(unsigned i=0;i<blocks;++i){DWORD written{};if(!WriteFile(pipe,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size())return 3;}
    const char tail[]="\nXR_EYE0_RECOMMENDED=1280x1280\n";
    DWORD written{};if(!WriteFile(pipe,tail,DWORD(sizeof(tail)-1),&written,nullptr))return 3;
    return 7;
}
static void scenario(const wchar_t* executable,const wchar_t* mode){
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE read{},write{};
    check(CreatePipe(&read,&write,&security,0)!=FALSE,"Cannot create probe pipe");
    check(SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)!=FALSE,"Cannot protect probe read handle");
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=write;startup.hStdError=write;
    PROCESS_INFORMATION process{};
    std::wstring command=L"\""+std::wstring(executable)+L"\" --child "+mode;
    const auto started=CreateProcessW(executable,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    CloseHandle(write);check(started!=FALSE,"Cannot start probe child");
    std::thread watchdog([&]{if(WaitForSingleObject(process.hProcess,3000)!=WAIT_OBJECT_0)TerminateProcess(process.hProcess,88);});
    const auto start=GetTickCount64();
    const bool normal=!std::wcscmp(mode,L"normal");
    const auto output=argent::launcher::collectProbeOutput(process.hProcess,read,normal?2000:200);
    const auto elapsed=GetTickCount64()-start;
    const auto exited=WaitForSingleObject(process.hProcess,1000)==WAIT_OBJECT_0;
    DWORD code{};GetExitCodeProcess(process.hProcess,&code);
    watchdog.join();CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(read);
    check(exited&&elapsed<2500,"Probe collection blocked or left its child running");
    if(normal){
        check(output==std::wstring(20*4096,L'x')+L"\nXR_EYE0_RECOMMENDED=1280x1280\n","Probe output truncated or lost its final bytes");
        check(code==7,"Normal probe was terminated");
    }else{
        check(output.find(!std::wcscmp(mode,L"flood")?L"reason=output-limit":L"reason=timeout")!=std::wstring::npos,"Probe failure was not bounded and reported");
        check(code==2,"Timed-out/flooding probe was not stopped");
    }
    std::wcout<<mode<<L" elapsedMs="<<elapsed<<L" bytes="<<output.size()<<L" exit="<<code<<L'\n';
}
int main(int argc,char** argv){try{
    if(argc==3&&!std::strcmp(argv[1],"--child"))return child(argv[2]);
    wchar_t executable[32768]{};const auto size=GetModuleFileNameW(nullptr,executable,32768);
    check(size&&size<32768,"Cannot resolve probe test executable");
    for(const auto* mode:{L"normal",L"silent",L"closed",L"flood"})scenario(executable,mode);
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
