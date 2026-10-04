#pragma once
#include "../openxr/ConfigurationFile.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace kharvox::hands {
inline std::string trimHandConfiguration(std::string value){
    const auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos)return {};
    const auto last=value.find_last_not_of(" \t\r\n");return value.substr(first,last-first+1);
}
inline DWORD readHandConfigurationFile(const std::filesystem::path& path,std::unordered_map<std::string,std::string>& values)noexcept{
    try{
        std::string bytes;const auto result=argent::input::readConfigurationFile(path,bytes);
        if(result!=ERROR_SUCCESS)return result;
        std::istringstream input(bytes);input.exceptions(std::ios::badbit);
        std::unordered_map<std::string,std::string> candidate;std::string line;
        while(std::getline(input,line)){
            line=trimHandConfiguration(std::move(line));if(line.empty()||line.front()=='#')continue;
            const auto equals=line.find('=');if(equals==std::string::npos)return ERROR_INVALID_DATA;
            const auto key=trimHandConfiguration(line.substr(0,equals));if(key.empty())return ERROR_INVALID_DATA;
            candidate[key]=trimHandConfiguration(line.substr(equals+1));
        }
        if(!input.eof())return ERROR_INVALID_DATA;
        values=std::move(candidate);return ERROR_SUCCESS;
    }catch(const std::bad_alloc&){return ERROR_NOT_ENOUGH_MEMORY;}catch(...){return ERROR_INVALID_DATA;}
}
template<size_t Count>
bool readHandConfigurationNumbers(const std::string& text,float(&values)[Count]){
    std::istringstream row(text);row.exceptions(std::ios::badbit);float candidate[Count]{};
    for(auto& value:candidate)if(!(row>>value)||!std::isfinite(value))return false;
    std::string suffix;if(row>>suffix&&suffix.front()!='#')return false;
    std::copy_n(candidate,Count,values);return true;
}
}
