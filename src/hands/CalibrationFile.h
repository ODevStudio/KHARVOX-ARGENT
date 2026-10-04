#pragma once
#include "../openxr/ConfigurationFile.h"
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace argent::calibration {
template<class Value,class Decode>
bool readCalibrationFile(const std::filesystem::path& path,std::map<std::string,Value>& values,Decode decode)noexcept{
    try{
        std::string bytes;
        if(input::readConfigurationFile(path,bytes)!=ERROR_SUCCESS)return false;
        std::istringstream input(bytes);auto candidate=values;std::string line;
        while(std::getline(input,line)){
            std::istringstream row(line);row.exceptions(std::ios::badbit);std::string key;
            if(!(row>>key)||key.front()=='#')continue;
            Value value{};
            if(!decode(row,value))return false;
            std::string suffix;
            if(row>>suffix&&suffix.front()!='#')return false;
            candidate[key]=value;
        }
        if(input.bad()||!input.eof())return false;
        values=std::move(candidate);return true;
    }catch(...){return false;}
}
}
