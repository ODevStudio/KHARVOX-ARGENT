#pragma once
#include <windows.h>
#include "WeaponHandling.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
namespace argent::input {
inline bool readSupportCalibration(std::istream& input,std::map<std::string,XrVector3f>& output){
 if(!input.good())return false;
 std::map<std::string,XrVector3f> candidate;std::string key;XrVector3f value{};
 while(input>>key){
  if(!(input>>value.x>>value.y>>value.z)||!camera::validPosition(value)||length(value)<.08f||length(value)>1.2f)return false;
  candidate[key]=value;
 }
 if(input.bad()||!input.eof())return false;
 output=std::move(candidate);return true;
}
inline bool saveSupportCalibration(const std::filesystem::path& config,const std::string& profile,const XrVector3f& value,std::map<std::string,XrVector3f>& profiles){
 if(config.empty())return false;
 auto saved=config;saved+=L".support";auto temporary=saved;temporary+=L".tmp";
 auto candidate=profiles;candidate[profile]=value;
 std::ofstream out(temporary);for(const auto& row:candidate)out<<row.first<<' '<<row.second.x<<' '<<row.second.y<<' '<<row.second.z<<'\n';
 out.flush();out.close();
 if(!out||!MoveFileExW(temporary.c_str(),saved.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return false;
 profiles=std::move(candidate);return true;
}
}
