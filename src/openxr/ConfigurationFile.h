#pragma once
#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
namespace argent::input {
inline DWORD readConfigurationFile(const std::filesystem::path& path,std::string& output){
 const auto handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(handle==INVALID_HANDLE_VALUE)return GetLastError();
 std::unique_ptr<void,decltype(&CloseHandle)> file(handle,&CloseHandle);
 std::string candidate;char buffer[4096];
 for(;;){
  DWORD bytes{};if(!ReadFile(file.get(),buffer,sizeof(buffer),&bytes,nullptr))return GetLastError();
  if(!bytes){output=std::move(candidate);return ERROR_SUCCESS;}
  candidate.append(buffer,bytes);
 }
}
}
