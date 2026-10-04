 std::filesystem::path configPath(){wchar_t name[32768]{};auto n=GetEnvironmentVariableW(L"ARGENT_LOG",name,32768);return n&&n<32768?std::filesystem::path(name).parent_path().parent_path()/"assets"/"argent_controls.cfg":std::filesystem::path{};}
 void refreshConfiguration(){
  if(GetTickCount64()-configRead<1000)return;configRead=GetTickCount64();
  try{
  auto file=configPath();if(file.empty())return;auto supportFile=file;supportFile+=L".support";
  std::string savedText;const auto savedResult=input::readConfigurationFile(supportFile,savedText);
  if(savedResult==ERROR_SUCCESS){std::istringstream saved(savedText);input::readSupportCalibration(saved,supportProfiles);}
  else if(savedResult==ERROR_FILE_NOT_FOUND||savedResult==ERROR_PATH_NOT_FOUND)supportProfiles.clear();
  std::string content;if(input::readConfigurationFile(file,content)!=ERROR_SUCCESS||content==configText)return;
  input::WeaponConfig candidate;std::istringstream rows(content);
  const bool accepted=input::readWeaponConfig(rows,candidate);
  if(accepted){configuration=std::move(candidate);support={};mapping={};motionWheel={};wheelClicks={};shoulder={};physicalKill={};handsJump={};handFilters={};presentation::optionBits=(configuration.automaticPresentation?1u:0u)|(configuration.cinematicsQuad?2u:0u)|(configuration.syncImmersive?4u:0u)|(configuration.movementImmersive?8u:0u)|(configuration.cinematics3d?16u:0u);}
  configText=std::move(content);
  if(accepted)log("XR_WEAPON_CONFIG pivotForward="+std::to_string(configuration.weaponPivot.x)+" pivotLeft="+std::to_string(configuration.weaponPivot.y)+" pivotUp="+std::to_string(configuration.weaponPivot.z)+" profile="+configuration.profile+" dominant="+(configuration.leftHanded?"left":"right")+" turn="+(configuration.snapTurn?"snap":"smooth")+" speed="+std::to_string(configuration.smoothSpeed)+" snap="+std::to_string(configuration.snapDegrees)+" physicalKill="+std::to_string(configuration.physicalGloryKill)+" punchSpeed="+std::to_string(configuration.glorySpeed)+" punchHands="+std::to_string(configuration.gloryHands)+" bhaptics="+std::to_string(configuration.bhapticsEnabled)+" psvr2="+std::to_string(configuration.psvr2AdaptiveTriggers));
  else log("XR_WEAPON_CONFIG rejected invalid configuration; keeping previous settings");
  }catch(...){}
 }
