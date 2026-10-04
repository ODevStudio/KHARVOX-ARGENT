void HandRenderer::Impl::loadCalibrationFile(const wchar_t* filename,bool defaults){
    try{
    auto left=leftCalibration,right=rightCalibration;auto profiles=weaponCalibrations;
    auto resetWeaponProfiles=[&]{
        for(auto& profile:profiles){profile[0]=left;profile[1]=right;}
    };
    std::unordered_map<std::string,std::string> values;
    const auto result=readHandConfigurationFile(root/filename,values);
    if(result!=ERROR_SUCCESS){
        if(defaults&&(result==ERROR_FILE_NOT_FOUND||result==ERROR_PATH_NOT_FOUND)){
            resetWeaponProfiles();weaponCalibrations=profiles;
            say("hand calibration defaults missing; using hand_models.cfg wrists");
        }
        return;
    }
    const auto version=values.find("version");
    if(version!=values.end()&&version->second!="1"&&version->second!="2")return;
    unsigned globalValues{},profileValues{};
    const auto vector3=[&](const std::string& key,float(&value)[3],unsigned& count){
        const auto found=values.find(key);if(found==values.end())return true;
        if(!readHandConfigurationNumbers(found->second,value))return false;
        ++count;return true;
    };
    if(!vector3("left_position",left.position,globalValues)||!vector3("left_rotation",left.rotationDegrees,globalValues)
        ||!vector3("right_position",right.position,globalValues)||!vector3("right_rotation",right.rotationDegrees,globalValues))return;
    if(defaults||version==values.end()||version->second!="2")resetWeaponProfiles();
    for(int value=static_cast<int>(HandWeaponKind::CombatShotgun);
        value<static_cast<int>(HandWeaponKind::Count);++value){
        const auto weapon=static_cast<HandWeaponKind>(value);
        const std::string key=HandWeaponKindKey(weapon);
        auto& profile=profiles[static_cast<size_t>(weapon)];
        if(!vector3(key+"_left_position",profile[0].position,profileValues)
            ||!vector3(key+"_left_rotation",profile[0].rotationDegrees,profileValues)
            ||!vector3(key+"_right_position",profile[1].position,profileValues)
            ||!vector3(key+"_right_rotation",profile[1].rotationDegrees,profileValues))return;
    }
    leftCalibration=left;rightCalibration=right;weaponCalibrations=profiles;
    if(globalValues||profileValues)
        say(std::string(defaults?"default":"saved")+" hand calibration loaded; per-weapon values="
            +std::to_string(profileValues));
    }catch(...){}
}
