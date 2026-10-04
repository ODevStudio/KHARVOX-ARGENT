#pragma once
#include <windows.h>
#include "WeaponHandling.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
namespace argent::input {
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
