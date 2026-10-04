#include "../src/vulkan/GpuRetirement.h"
#include "../src/openxr/GameImageLifetime.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>

namespace argent {void log(const std::string& message){std::cout<<message<<'\n';}}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

static int child(VkResult result){
    kharvox::GameImageLifetime lifetime;
    const auto image=reinterpret_cast<VkImage>(uintptr_t(1));
    std::atomic<bool> destroyed{};
    std::future<void> retiring;
    {
        kharvox::GameImageLifetime::Use use(lifetime);
        check(use.select([]{return true;},[&]{return std::array{kharvox::GameImageLifetime::key(image)};}),"Cannot lease test image");
        std::promise<void> started;
        retiring=std::async(std::launch::async,[&]{started.set_value();lifetime.retire(kharvox::GameImageLifetime::key(image),[&]{destroyed=true;});});
        started.get_future().wait();
        check(retiring.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout,"Borrowed image retired before completion gate");
        const auto retired=argent::requireGpuRetirement(result,"test copy");
        if(retired!=VK_SUCCESS&&retired!=VK_ERROR_DEVICE_LOST)ExitProcess(86);
        check(retired==result&&!destroyed,"Device loss was misreported as successful completion");
    }
    retiring.get();
    check(destroyed,"Completed image lease was not released");
    return 0;
}

static DWORD run(const wchar_t* executable,VkResult result){
    std::wstring command=L"\""+std::wstring(executable)+L"\" "+std::to_wstring(result);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Cannot start retirement check");
    const auto wait=WaitForSingleObject(process.hProcess,5000);
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,88);WaitForSingleObject(process.hProcess,5000);}
    DWORD code{};const bool exited=GetExitCodeProcess(process.hProcess,&code)!=FALSE;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    check(wait==WAIT_OBJECT_0&&exited,"Retirement check timed out or has no result");
    return code;
}

int main(int argc,char** argv){try{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==2)return child(static_cast<VkResult>(std::atoi(argv[1])));
    wchar_t executable[32768]{};
    const auto size=GetModuleFileNameW(nullptr,executable,32768);
    check(size&&size<32768,"Cannot resolve retirement test executable");
    for(const auto result:{VK_SUCCESS,VK_ERROR_DEVICE_LOST,VK_ERROR_OUT_OF_HOST_MEMORY,VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_ERROR_INITIALIZATION_FAILED,VK_TIMEOUT,VK_NOT_READY}){
        const auto status=run(executable,result);
        std::cout<<"result="<<result<<" exit=0x"<<std::hex<<status<<std::dec<<'\n';
        check(status==((result==VK_SUCCESS||result==VK_ERROR_DEVICE_LOST)?0u:0xc0000602u),"Unverified GPU retirement allowed resource release");
    }
    std::cout<<"GPU retirement gates preserve borrowed images and distinguish device loss from completion\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
