#pragma once
#include <windows.h>
#include <algorithm>
#include <string>

namespace argent::launcher {
inline std::wstring collectProbeOutput(HANDLE process,HANDLE pipe,DWORD timeout=15000){
    std::wstring output;
    const auto start=GetTickCount64();
    bool pipeOpen=true;
    const auto stop=[&](const wchar_t* reason){
        output+=L"\nPROBE_CAPTURE_FAILED reason="+std::wstring(reason)+L"\n";
        if(!TerminateProcess(process,2))output+=L"PROBE_STOP_FAILED win32="+std::to_wstring(GetLastError())+L"\n";
        else if(WaitForSingleObject(process,5000)!=WAIT_OBJECT_0)output+=L"PROBE_STOP_FAILED wait\n";
        return output;
    };
    for(;;){
        const auto elapsed=GetTickCount64()-start;
        if(elapsed>=timeout)return stop(L"timeout");
        DWORD available{};
        if(pipeOpen&&!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)){
            if(GetLastError()!=ERROR_BROKEN_PIPE)return stop(L"pipe-peek");
            pipeOpen=false;
        }
        if(available){
            char buffer[4096];DWORD read{};
            if(!ReadFile(pipe,buffer,(std::min)(available,DWORD(sizeof(buffer))),&read,nullptr))return stop(L"pipe-read");
            if(output.size()+read>1024*1024)return stop(L"output-limit");
            output.append(buffer,buffer+read);
            continue;
        }
        const auto waited=WaitForSingleObject(process,(std::min)(DWORD(timeout-elapsed),DWORD(20)));
        if(waited==WAIT_FAILED)return stop(L"process-wait");
        if(waited==WAIT_OBJECT_0){
            if(pipeOpen&&PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr)&&available)continue;
            return output;
        }
    }
}
}
