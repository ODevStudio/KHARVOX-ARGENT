#pragma once
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
namespace argent::launcher {
inline std::string updateSettings(std::istream& input,std::map<std::string,std::string> values){
 if(!input.good())throw std::runtime_error("Cannot read existing settings");
 std::ostringstream output;std::string line;std::set<std::string> written;
 while(std::getline(input,line)){
  const auto text=!line.empty()&&line.back()=='\r'?line.substr(0,line.size()-1):line;
  std::istringstream row(text);std::string key;row>>key;
  auto value=values.find(key);
  if(value!=values.end()){if(written.insert(key).second)output<<key<<' '<<value->second<<'\n';}
  else output<<text<<'\n';
 }
 if(input.bad()||!input.eof())throw std::runtime_error("Cannot read complete settings");
 for(const auto& value:values)if(!written.count(value.first))output<<value.first<<' '<<value.second<<'\n';
 if(!output)throw std::runtime_error("Cannot format complete settings");
 return output.str();
}
}
