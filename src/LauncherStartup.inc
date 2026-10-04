std::map<std::string,std::string> controlsSettings() {
    wchar_t angle[16]{},speed[16]{},kill[16]{};GetWindowTextW(snapAngle,angle,16);GetWindowTextW(smoothSpeed,speed,16);GetWindowTextW(killSpeed,kill,16);
    std::map<std::string,std::string> values{
      {"dominant",isChecked(dominantHand)?"left":"right"},
      {"left_hand_swap",selected(leftHandSwap)==1?"buttons-and-sticks":"buttons"},
      {"show_hands",isChecked(showHands)?"1":"0"},
      {"laser_sight","0"},
      {"hands_jump",isChecked(handsJump)?"1":"0"},
      {"hand_smoothing",isChecked(handSmoothing)?"1":"0"},
      {"cinematics_3d",isChecked(cinematics3d)?"1":"0"},
      {"weapon_pivot",std::to_string((selected(weaponPivot[0])-60)*.005f-.15f)+" "+std::to_string((selected(weaponPivot[1])-60)*.005f)+" "+std::to_string((selected(weaponPivot[2])-60)*.005f)},
      {"calibration_mode",calibrationModes[std::clamp(selected(calibrationMode),0,4)]},
      {"calibration_apply",std::to_string(calibrationApplyCommand.revision)},
      {"calibration_apply_profile",calibrationApplyCommand.profile},
      {"calibration_apply_mode",calibrationApplyCommand.mode},
      {"calibration_apply_left",calibrationApplyCommand.left?"1":"0"},
      {"profile",calibrationProfiles.at(std::max(0,selected(calibrationProfile)))},
      {"movement",selected(movement)==1?"offhand":"head"},
      {"turn",selected(turnMode)==1?"snap "+std::to_string(_wtoi(angle)):"smooth"},
      {"smooth_turn_speed",std::to_string(_wtoi(speed))},
      {"snap_turn_angle",std::to_string(_wtoi(angle))},
      {"physical_glory_kill",isChecked(physicalKill)?"1":"0"},
      {"physical_glory_kill_speed",std::string(kill,kill+wcslen(kill))},
      {"physical_glory_kill_hands",selected(killHands)==0?"left":selected(killHands)==1?"right":"both"},
      {"controller_layout","argent"},
      {"automatic_presentation","1"},
      {"cinematics_quad","1"},
      {"sync_immersive","1"},
      {"movement_immersive","1"},
      {"shoulder_chainsaw","1"},
      {"virtual_gunstock",isChecked(virtualGunstock)?"1":"0"},
      {"bhaptics_enabled",isChecked(bhaptics)?"1":"0"},
      {"psvr2_adaptive_triggers",isChecked(psvr2Triggers)?"1":"0"},
      {"crouch_enabled","0"},
      {"two_hand_enabled",isChecked(virtualGunstock)?"1":"0"}};
    return values;
}
std::map<std::wstring,HWND> launcherChecks(){
    if constexpr(cleanRelease)return {{L"desktopMirror",desktopMirror},{L"fsr",fsr},{L"extendedLoggingOptIn",extendedLogging},{L"captureShaders",captureShaders},{L"gpuCrashDiagnosticsOptIn",gpuDiagnostics}};
    return {{L"desktopMirror",desktopMirror},{L"fsr",fsr},{L"extendedLogging",extendedLogging},{L"disableAa",disableAa},
        {L"captureShaders",captureShaders},{L"simulator",simulator},{L"gpuCrashDiagnosticsOptIn",gpuDiagnostics}};
}
void saveSettings(){
    const auto settings=launcherSettingsPath();std::filesystem::create_directories(settings.parent_path());
    const auto temporary=settings.wstring()+L".tmp";
    // Preserve unknown keys; replace the complete snapshot only after all writes succeed.
    if(std::filesystem::exists(settings))std::filesystem::copy_file(settings,temporary,std::filesystem::copy_options::overwrite_existing);
    else {std::ofstream fresh(temporary,std::ios::binary|std::ios::trunc);fresh.write("\xff\xfe",2);fresh.close();if(!fresh)throw std::runtime_error("Cannot create settings");}
    auto put=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value){
        if(!WritePrivateProfileStringW(section,key,value.c_str(),temporary.c_str()))throw std::runtime_error("Cannot save settings");
    };
    put(L"launcher",L"renderer",L"1");
    put(L"launcher",L"mirrorResolution",std::to_wstring(selected(mirrorResolution)));
    put(L"launcher",L"vrIntroStarted",vrIntroStarted?L"1":L"0");
    put(L"launcher",L"disableVrIntro",isChecked(disableVrIntro)?L"1":L"0");
    wchar_t scaleLabel[16]{};GetWindowTextW(renderScale,scaleLabel,16);
    put(L"launcher",L"renderScalePercent",std::to_wstring(_wtoi(scaleLabel)));
    for(const auto& setting:launcherChecks())put(L"launcher",setting.first.c_str(),isChecked(setting.second)?L"1":L"0");
    wchar_t gamePath[32768]{};GetWindowTextW(gameEdit,gamePath,32768);put(L"launcher",L"game",gamePath);
    for(const auto& setting:controlsSettings()){
        const std::wstring key(setting.first.begin(),setting.first.end()),value(setting.second.begin(),setting.second.end());
        put(L"controls",key.c_str(),value);
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    if(!MoveFileExW(temporary.c_str(),settings.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings");
}
bool autoSaveSettings(){
    if(!settingsReady)return true;
    try{saveSettings();return true;}catch(...){setStatus(L"Settings could not be saved. Check access to LocalAppData/KHARVOX ARGENT.");return false;}
}
#include "LauncherControlsSave.inc"
std::filesystem::path legacyControlsPath(){
    const auto fallback=root/L"assets/argent_controls.cfg";
    const auto ini=launcherSettingsPath();
    if(GetPrivateProfileIntW(L"controls",L"automatic_presentation",-1,ini.c_str())!=-1)return fallback;
    std::error_code ec;const auto stamp=std::filesystem::last_write_time(ini,ec);if(ec)return fallback;
    // Older launchers wrote their local controls immediately before the shared
    // INI. Only import a sibling release whose save timestamp matches that INI.
    auto result=fallback;auto closest=std::chrono::duration_cast<std::filesystem::file_time_type::duration>(std::chrono::seconds(2));
    for(const auto& entry:std::filesystem::directory_iterator(root.parent_path(),ec)){
        if(!std::regex_match(entry.path().filename().string(),std::regex("ARGENT-Alpha-Test-r[0-9]{3}")))continue;
        const auto path=entry.path()/L"assets/argent_controls.cfg";
        const auto time=std::filesystem::last_write_time(path,ec);if(ec){ec.clear();continue;}
        const auto difference=time>stamp?time-stamp:stamp-time;
        if(difference<closest){closest=difference;result=path;}
    }
    return result;
}
void updateLeftHandLayout(){
    const bool on=isChecked(dominantHand);
    EnableWindow(leftHandSwap,on);
    ShowWindow(leftHandSwap,on?SW_SHOW:SW_HIDE);
    ShowWindow(leftHandLayoutLabel,on?SW_SHOW:SW_HIDE);
}
void loadSettings(){
    struct Restore {bool previous=settingsReady;Restore(){settingsReady=false;}~Restore(){settingsReady=previous;}} restore;
    auto ini=launcherSettingsPath().wstring();
    SendMessageW(mirrorResolution,CB_SETCURSEL,std::clamp(int(GetPrivateProfileIntW(L"launcher",L"mirrorResolution",1,ini.c_str())),0,3),0);
    vrIntroStarted=GetPrivateProfileIntW(L"launcher",L"vrIntroStarted",0,ini.c_str())!=0;
    setChecked(disableVrIntro,GetPrivateProfileIntW(L"launcher",L"disableVrIntro",0,ini.c_str())!=0);
    ShowWindow(disableVrIntro,vrIntroStarted?SW_SHOW:SW_HIDE);
    int scale=GetPrivateProfileIntW(L"launcher",L"renderScale",5,ini.c_str());
    int percent=GetPrivateProfileIntW(L"launcher",L"renderScalePercent",scale>=0&&scale<=5?50+scale*10:100,ini.c_str());
    SendMessageW(renderScale,CB_SETCURSEL,(std::clamp(percent,40,200)-40+2)/5,0);
    for(const auto& setting:launcherChecks())setChecked(setting.second,GetPrivateProfileIntW(L"launcher",setting.first.c_str(),0,ini.c_str())!=0);
    wchar_t game[32768]{};GetPrivateProfileStringW(L"launcher",L"game",L"",game,32768,ini.c_str());
    if(game[0]&&validGameExecutable(game))SetWindowTextW(gameEdit,game);
    else SetWindowTextW(gameEdit,findGameExecutable().c_str());
    std::ifstream defaults(legacyControlsPath());std::stringstream file;file<<defaults.rdbuf();file.clear();file<<'\n';
    // Release defaults and legacy package preferences are fallback only.
    // Central values win when a new release ships a fresh controls file.
    for(const auto& setting:controlsSettings()){
        const std::wstring key(setting.first.begin(),setting.first.end());wchar_t value[2048]{};
        if(GetPrivateProfileStringW(L"controls",key.c_str(),L"",value,2048,ini.c_str()))
            file<<setting.first<<' '<<std::string(value,value+wcslen(value))<<'\n';
    }
    std::string line;
    while(std::getline(file,line)){
        std::istringstream row(line);std::string key,value;row>>key>>value;
        for(auto setting:{std::pair{"virtual_gunstock",virtualGunstock},{"two_hand_enabled",virtualGunstock},{"bhaptics_enabled",bhaptics},{"psvr2_adaptive_triggers",psvr2Triggers},{"physical_glory_kill",physicalKill}})
            if(key==setting.first&&(value=="0"||value=="1"))setChecked(setting.second,value=="1");
        if(key=="weapon_pivot"){
            std::istringstream pivotRow(line.substr(key.size()));float v[3]{};
            if(pivotRow>>v[0]>>v[1]>>v[2])for(int i=0;i<3;++i)if(std::isfinite(v[i])&&v[i]>=(i==0?-.45f:-.3f)&&v[i]<=(i==0?.15f:.3f))SendMessageW(weaponPivot[i],CB_SETCURSEL,int(std::lround((v[i]+(i==0?.15f:0.f))/.005f))+60,0);
        }
        if(key=="show_hands")setChecked(showHands,value=="1");
        if(key=="hands_jump")setChecked(handsJump,value=="1");
        if(key=="hand_smoothing")setChecked(handSmoothing,value=="1");
        if(key=="cinematics_3d")setChecked(cinematics3d,value=="1");
        if(key=="calibration_mode"){for(int i=0;i<5;++i)if(value==calibrationModes[i])SendMessageW(calibrationMode,CB_SETCURSEL,i,0);}
        if(key=="profile"){
            auto it=std::find(calibrationProfiles.begin(),calibrationProfiles.end(),value);
            if(it==calibrationProfiles.end()){calibrationProfiles.push_back(value);std::wstring name(value.begin(),value.end());SendMessageW(calibrationProfile,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));it=calibrationProfiles.end()-1;}
            SendMessageW(calibrationProfile,CB_SETCURSEL,it-calibrationProfiles.begin(),0);
        }
        if(key=="smooth_turn_speed"){int v=atoi(value.c_str());SendMessageW(smoothSpeed,CB_SETCURSEL,(std::clamp(v,200,300)-200+2)/5,0);}
        if(key=="snap_turn_angle"){int v=atoi(value.c_str());SendMessageW(snapAngle,CB_SETCURSEL,(std::clamp(v,45,90)-45+2)/5,0);}
        if(key=="physical_glory_kill_speed"){float v=float(atof(value.c_str()));if(v>=1&&v<=4)SendMessageW(killSpeed,CB_SETCURSEL,int((v-1)*5+.5f),0);}
        if(key=="physical_glory_kill_hands")SendMessageW(killHands,CB_SETCURSEL,value=="left"?0:value=="right"?1:2,0);
        if(key=="dominant")setChecked(dominantHand,value=="left");
        if(key=="left_hand_swap")SendMessageW(leftHandSwap,CB_SETCURSEL,value=="buttons-and-sticks",0);
        if(key=="movement")SendMessageW(movement,CB_SETCURSEL,value=="offhand",0);
        if(key=="turn"){
            SendMessageW(turnMode,CB_SETCURSEL,value=="snap",0);
            if(value=="snap"){int degrees{};row>>degrees;SendMessageW(snapAngle,CB_SETCURSEL,(std::clamp(degrees,45,90)-45+2)/5,0);}
        }
    }
    EnableWindow(snapAngle,TRUE);EnableWindow(smoothSpeed,TRUE);updateLeftHandLayout();
}

bool playNativeVrIntro(std::wofstream& log,const wchar_t* argument=L"--vr-intro"){
    if(vrIntroStarted&&isChecked(disableVrIntro))return true;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::wstring command=quote(executable)+L" "+argument;
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&si,&pi)){
        setStatus(L"Cannot start native VR intro. Doom was not started.");return false;
    }
    CloseHandle(pi.hThread);introThreadId=pi.dwThreadId;introCancelled=false;
    vrIntroStarted=true;ShowWindow(disableVrIntro,SW_SHOW);autoSaveSettings();
    setStatus(L"VR intro running. Press a controller button to continue to Doom Eternal.");
    if(log)log<<L"Native VR intro started: PID "<<pi.dwProcessId<<L". Waiting for complete OpenXR shutdown.\n"<<std::flush;
    bool finished=false;
    while(!finished){
        const auto result=MsgWaitForMultipleObjects(1,&pi.hProcess,FALSE,100,QS_ALLINPUT);
        if(result==WAIT_OBJECT_0){finished=true;break;}
        if(result==WAIT_FAILED)break;
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){introCancelled=true;PostThreadMessageW(introThreadId,WM_QUIT,0,0);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(introCancelled)PostThreadMessageW(introThreadId,WM_QUIT,0,0);
    }
    DWORD code=1;if(finished)GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);introThreadId=0;
    if(log)log<<L"Native VR intro exited: "<<code<<L" cancelled="<<introCancelled<<L"\n"<<std::flush;
    if(introCancelled){DestroyWindow(GetParent(statusText));return false;}
    if(!finished||code!=0){setStatus(L"VR intro did not complete. Retry, or enable Disable VR Intro under Rendering.");return false;}
    setStatus(L"VR intro finished. Starting Doom Eternal...");return true;
}
void runLauncher(bool launch) {
    if(launchInProgress)return;
    struct Busy {Busy(){launchInProgress=true;}~Busy(){launchInProgress=false;}} busy;
    wchar_t gamePath[32768]{};
    GetWindowTextW(gameEdit, gamePath, 32768);
    if (!std::filesystem::is_regular_file(gamePath)) {
        setStatus(L"Eternal executable not found. Select DOOMEternalx64vk.exe.");
        return;
    }
    if (argent::mouse::gameRunning()) {
        setStatus(L"DOOM Eternal is already running. Close it before starting a VR session.");
        return;
    }
    if (!argent::mouse::repairSavedMouse()) {
        setStatus(L"Cannot restore mouse settings. Check access to the DOOM Eternal Saved Games folder.");
        return;
    }
    const bool storeGame=microsoftStoreGame(gamePath);
    bool elevationKnown{};
    if (currentProcessElevated(elevationKnown)) {
        setStatus(storeGame ? L"ARGENT is running as administrator. Start ARGENT and DOOM normally."
                            : L"ARGENT is running as administrator. Start ARGENT, Steam, and DOOM normally.");
        return;
    }
    if (!elevationKnown) {
        setStatus(storeGame ? L"Cannot verify launcher elevation. Start ARGENT and DOOM normally."
                            : L"Cannot verify launcher elevation. Start ARGENT, Steam, and DOOM normally.");
        return;
    }
    std::wstring runAsLocation;
    bool runAsKnown{};
    if (doomRunAsAdmin(gamePath, runAsLocation, runAsKnown)) {
        setStatus(L"DOOM Eternal is configured to run as administrator. Disable that Compatibility setting.");
        return;
    }
    if (!runAsKnown) {
        setStatus(L"Cannot verify DOOM administrator Compatibility settings.");
        return;
    }
    if(!storeGame){
        bool steamKnown{};
        if (runningSteamElevated(steamKnown)) {
            setStatus(L"Steam is running as administrator. Exit Steam and start it normally.");
            return;
        }
        if (!steamKnown) {
            setStatus(L"Cannot verify Steam elevation. Start Steam normally, not as administrator.");
            return;
        }
    }

    try{writeControlsConfig();}catch(...){setStatus(L"Cannot save settings. Check write access to the package folder.");return;}
    std::filesystem::create_directories(root / L"logs");
    std::wofstream log(root / L"logs/launcher-output.txt", std::ios::trunc);
    if(log)log<<L"Game distribution: "<<(storeGame?L"Microsoft Store":L"Steam/other")<<L"; executable="<<gamePath<<L"\n";
    auto fail = [&](const wchar_t* message){ if(log)log<<message<<L"\n"; setStatus(message); };
    const auto runtime = std::filesystem::exists(root / L"ArgentLayer.dll") ? root : root / L"build/Release";
    for (auto name : {L"ArgentRuntimeProbe.exe", L"ArgentLayer.dll", L"ArgentLayer.json", L"openxr_loader.dll"}) {
        if (!std::filesystem::exists(runtime / name)) { fail(L"Required ARGENT runtime file missing."); return; }
    }
    constexpr int mode = 1; // Stereo VR is the only launcher rendering mode.
    const auto runtimeManifest = activeOpenXRManifestW();
    const bool steamVr = usesSteamVrRuntime();
    updateRenderingAvailability(steamVr);
    std::map<std::wstring,std::wstring> probeEnv;
    if (isChecked(simulator)) probeEnv[L"XR_RUNTIME_JSON"] = (root / L"logs/simulator-runtime.json").wstring();
    else if (steamVr) addSteamVrPathEnvironment(probeEnv, runtimeManifest);
    setStatus(launch ? L"Checking runtime and starting DOOM Eternal..." : L"Checking Vulkan, OpenXR and headset...");
    auto probe = processOutput(runtime / L"ArgentRuntimeProbe.exe", probeEnv);
    if (log) log << probe << L"\n";
    if(containsLine(probe,L"PROBE_START_FAILED")||containsLine(probe,L"PROBE_CAPTURE_FAILED")){
        fail(L"Runtime probe failed or timed out. DOOM was not started.");return;
    }
    if (!launch && mode == 1 && !containsLine(probe, L"XR_EYE0_RECOMMENDED=")) { fail(L"VR diagnostics failed. Connect the headset and start its OpenXR runtime."); return; }
    if (!launch) { setStatus(L"Runtime check complete. See Development logs."); return; }
    if (steamVr) ensureSteamVrArgentSettings(log);
    wchar_t scaleText[16]{};
    GetWindowTextW(renderScale, scaleText, 16);
    const double scale = steamVr ? 1.0 : std::clamp(_wtoi(scaleText) / 100.0, .4, 2.);
    const bool useFsr = mode==1 && !steamVr && isChecked(fsr);
    const double engineScale = useFsr && scale<1.0 ? 1.0 : scale;
    wchar_t scaleEnv[32]{};
    swprintf_s(scaleEnv, L"%.2f", engineScale);
    std::map<std::wstring,std::wstring> env;
    env[L"VK_LAYER_PATH"] = runtime.wstring();
    env[L"VK_INSTANCE_LAYERS"] = L"VK_LAYER_ARGENT_OPENXR";
    env[L"ARGENT_ENABLE_LAYER"] = L"1";
    env[L"ARGENT_LOG"] = (root / L"logs" / (L"quad-" + timestampName() + L".log")).wstring();
    try {
        const auto capture=configureShaderCapture(env,isChecked(captureShaders),root,
            timestampName()+L"-"+std::to_wstring(GetCurrentProcessId()));
        if(log)log<<(capture.empty()?L"Shader capture: disabled":L"Shader capture: "+capture.wstring())<<L"\n";
    } catch(const std::exception&) {
        fail(L"Cannot create shader capture. Check write access to the package captures folder.");
        return;
    }
    if(!storeGame){
        env[L"SteamAppId"] = L"782330";
        env[L"SteamGameId"] = L"782330";
    }else{
        env[L"SteamAppId"] = L"";
        env[L"SteamGameId"] = L"";
    }
    if (steamVr) addSteamVrPathEnvironment(env, runtimeManifest);
    env[L"ARGENT_RENDER_SCALE"] = scaleEnv;
    env[L"ARGENT_FSR1"] = useFsr ? L"1" : L"0";
    configureStereoDlss(env,mode);
    if(log&&mode==1)log<<L"Stereo DLSS: independent eye histories enabled; engine selection is respected when FSR 1 is off.\n";
    const bool performanceDiagnostics=!cleanRelease&&std::filesystem::exists(root/L"assets/performance-diagnostics.cfg");
    env[L"ARGENT_PERFORMANCE_DIAGNOSTICS"] = performanceDiagnostics ? L"1" : L"0";
    env[L"ARGENT_DISABLE_AA"] = L"0";
    env[L"ARGENT_EXTENDED_LOGGING"] = performanceDiagnostics||isChecked(extendedLogging) ? L"1" : L"0";
    env[L"ARGENT_SFS_PROFILE_TIMING"] = !performanceDiagnostics&&isChecked(extendedLogging) ? L"1" : L"0";
    configureGpuDiagnostics(env,isChecked(gpuDiagnostics));
    if(log)log<<L"Clean release: "<<cleanRelease<<L". Native TAA blocked; DLSS allowed; engine setter/readback. Extended logging: "<<isChecked(extendedLogging)<<L"; GPU crash diagnostics: "<<isChecked(gpuDiagnostics)<<L"; continuous profiling: "<<performanceDiagnostics<<L"\n";
    env[L"ARGENT_GPU_TIMING"] = L"0";
    env[L"ARGENT_DESKTOP_MIRROR"] = isChecked(desktopMirror) ? L"1" : L"0";
    env[L"ARGENT_MIRROR_MAX_FPS"] = L"60";
    const int mirrorHeights[]{720,1080,1440,2160};
    const auto desktop=argent::desktopMirrorExtent(isChecked(desktopMirror),mirrorHeights[std::clamp(selected(mirrorResolution),0,3)]);
    env[L"ARGENT_MIRROR_HEIGHT"] = std::to_wstring(desktop.height);
    for (auto key : {L"DISABLE_VK_LAYER_VALVE_steam_overlay_1",L"DISABLE_VK_LAYER_VALVE_steam_fossilize_1",L"DISABLE_VULKAN_OBS_CAPTURE",L"EOS_OVERLAY_DISABLE_VULKAN_WIN64",L"DISABLE_VK_LAYER_RealVR_1",
        L"DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY"})
        env[key] = L"1";
    std::wstring args = L"+com_skipIntroVideo 1 +r_swapInterval 0 +r_filmGrainRatio 0 +r_motionblur 0 +g_autoMotionBlurOnGK 0 +r_chromaticAberration 0";
    args += L" +r_skipFlares 1 +r_lensFlaresRatio 0 +r_cineLensflaresEnabled 0 +view_skipShakes 1 +view_mpViewKick 0 +g_setting_hands_bob 0 +pm_noBob 1";
    wchar_t scaleArg[96]{};
    swprintf_s(scaleArg, L" +r_enableResolutionScale 1 +rs_enable 0 +rs_forceResolution %.2f", engineScale);
    args += scaleArg;
    if(useFsr)args += L" +r_antialiasing 0 +r_TAASafeMode 1";
    if (mode == 1 || mode == 2) {
        env[L"ARGENT_SFS_NATIVE_PROBE"] = L"1";
        env[L"ARGENT_SFS_NATIVE_VR"] = mode == 1 ? L"1" : L"0";
        if (steamVr && mode == 1) {
            // KHARVOX's working SteamXR path lets SteamVR create the Vulkan
            // device through XR_KHR_vulkan_enable2. Explicit zeroes also mask
            // stale process-wide overrides left by older ARGENT builds.
            env[L"ARGENT_STEAMVR_DEFER_XR_DEVICE"] = L"0";
            env[L"ARGENT_STEAMVR_FORCE_ENABLE1"] = L"0";
            if (log) log << L"SteamVR SFS: native VR enabled with KHARVOX runtime-managed XR device creation.\n";
            if (log) log << L"SteamVR SFS: using XR_KHR_vulkan_enable2 when exposed by SteamVR.\n";
        }
        if (mode == 1) {
            int eyeWidth = maxEyeValue(probe, true);
            int eyeHeight = maxEyeValue(probe, false);
            if ((eyeWidth <= 0 || eyeHeight <= 0) && steamVr) {
                eyeWidth = 1844;
                eyeHeight = 1972;
                if (log) log << L"SteamVR probe did not report eye size; using startup fallback 1844x1972 and allowing in-game OpenXR initialization.\n";
            }
            if (eyeWidth <= 0 || eyeHeight <= 0) {
                fail(L"VR diagnostics failed. Connect the headset and start its OpenXR runtime.");
                return;
            }
            const auto resolution=argent::renderResolution(uint32_t(eyeWidth),uint32_t(eyeHeight),float(scale),useFsr);
            if(log)log<<L"Render source: "<<resolution.sourceWidth<<L"x"<<resolution.sourceHeight<<L"; headset: "<<eyeWidth<<L"x"<<eyeHeight<<L"; engine scale: "<<engineScale<<L"; FSR upscale: "<<resolution.fsrUpscale<<L"\n";
            eyeWidth=int(resolution.sourceWidth);eyeHeight=int(resolution.sourceHeight);
            env[L"ARGENT_SFS_SOURCE_RING"] = L"1";
            env[L"ARGENT_EYE_WIDTH"] = std::to_wstring(eyeWidth);
            env[L"ARGENT_EYE_HEIGHT"] = std::to_wstring(eyeHeight);
            if (steamVr && log) {
                log << L"SteamVR SFS: KHARVOX-style source ring and render-extent hooks enabled for 2-layer stereo images.\n";
            }
            if(!storeGame)CopyFileW((root / L"assets/argent_vr.cfg").c_str(), (std::filesystem::path(gamePath).parent_path() / L"base/argent_vr.cfg").c_str(), FALSE);
            args += L" +r_fullscreen 0 +r_windowWidth "+std::to_wstring(desktop.width)+L" +r_windowHeight "+std::to_wstring(desktop.height)+L" +g_fov 90 +hands_fovScale 1 +in_joystick 1 +in_mouse 0 +in_MarkJoystickInactiveOnMouseInput 0 +g_reticleMode 2";
            if(!storeGame)args+=L" +exec argent_vr.cfg";
        }
        args += L" +r_enableRayTracing 0 +r_enableResolutionScale 1 +r_hdrDisplay 0 +r_presentFromAsync 1 +r_SSR 0";
        // Restore the values observed before r182, including persisted settings.
        args += L" +r_waterInterleaveUpdates 1 +r_waterGridTAA 1 +r_waterHalfRes 1";
        if(useFsr)args += L" +r_antialiasing 0 +r_TAASafeMode 1";
    }
    std::wstring bhPipe, bhToken, psPipe, psToken;
    if (isChecked(bhaptics) && std::filesystem::exists(runtime / L"KharvoxBhapticsBridge.exe") && std::filesystem::exists(runtime / L"bhaptics_library.dll")) {
        bhPipe = L"KharvoxBhaptics_" + hexToken(); bhToken = hexToken() + hexToken();
        env[L"KHARVOX_BHAPTICS_PIPE_NAME"] = bhPipe; env[L"KHARVOX_BHAPTICS_SESSION_TOKEN"] = bhToken;
    }
    if (isChecked(psvr2Triggers) && std::filesystem::exists(runtime / L"KharvoxPsvr2Bridge.exe") && std::filesystem::exists(runtime / L"psvr2_toolkit_capi_loader.dll")) {
        psPipe = L"KharvoxPsvr2_" + hexToken(); psToken = hexToken() + hexToken();
        env[L"KHARVOX_USE_PSVR2_TOOLKIT"] = L"1"; env[L"KHARVOX_PSVR2_PIPE_NAME"] = psPipe; env[L"KHARVOX_PSVR2_SESSION_TOKEN"] = psToken;
    }
    if(mode!=2&&!playNativeVrIntro(log))return;
    auto block = GetEnvironmentStringsW();
    std::wstring environment;
    if (block) {
        for (auto p = block; *p; p += wcslen(p) + 1) {
            std::wstring row(p); auto eq = row.find(L'='); auto key = eq == std::wstring::npos ? row : row.substr(0, eq);
            bool replaced=false; for (const auto& entry:env) if (_wcsicmp(key.c_str(),entry.first.c_str())==0) replaced=true;
            if(!replaced){environment+=row;environment.push_back(L'\0');}
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& entry:env){environment+=entry.first+L"="+entry.second;environment.push_back(L'\0');}
    environment.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring command = quote(gamePath) + L" " + args;
    BOOL ok = CreateProcessW(gamePath, command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED, environment.data(), std::filesystem::path(gamePath).parent_path().c_str(), &si, &pi);
    if (!ok) {
        const auto error = GetLastError();
        auto message = L"Cannot start DOOM Eternal. win32=" + std::to_wstring(error);
        if (log) log << message << L"\n";
        setStatus(message.c_str());
        return;
    }
    if (!argent::mouse::startRestoreMonitor(pi.hProcess)) {
        TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        fail(L"Cannot start mouse restoration helper. DOOM was not started.");return;
    }
    if (ResumeThread(pi.hThread)==DWORD(-1)) {
        TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        fail(L"Cannot resume DOOM Eternal startup.");return;
    }
    auto startBridge=[&](const wchar_t* exe,const std::map<std::wstring,std::wstring>& extra){
        if(!std::filesystem::exists(runtime/exe))return;
        auto bridgeEnv=env; for(auto& e:extra)bridgeEnv[e.first]=e.second;
        bridgeEnv[L"KHARVOX_BHAPTICS_PARENT_PID"]=std::to_wstring(pi.dwProcessId);
        bridgeEnv[L"KHARVOX_PSVR2_PARENT_PID"]=std::to_wstring(pi.dwProcessId);
        bridgeEnv[L"KHARVOX_EXTENDED_LOGGING"]=isChecked(extendedLogging)?L"1":L"0";
        auto bridgeBlock=GetEnvironmentStringsW(); std::wstring be;
        if(bridgeBlock){for(auto p=bridgeBlock;*p;p+=wcslen(p)+1){std::wstring row(p);auto eq=row.find(L'=');auto key=eq==std::wstring::npos?row:row.substr(0,eq);bool rep=false;for(auto& e:bridgeEnv)if(_wcsicmp(key.c_str(),e.first.c_str())==0)rep=true;if(!rep){be+=row;be.push_back(L'\0');}}FreeEnvironmentStringsW(bridgeBlock);}
        for(auto& e:bridgeEnv){be+=e.first+L"="+e.second;be.push_back(L'\0');}be.push_back(L'\0');
        STARTUPINFOW bsi{};bsi.cb=sizeof(bsi);PROCESS_INFORMATION bpi{};
        std::wstring bcmd=quote((runtime/exe).wstring());
        if(CreateProcessW((runtime/exe).c_str(),bcmd.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,be.data(),runtime.c_str(),&bsi,&bpi)){
            if(log)log<<L"Bridge started: "<<exe<<L" PID="<<bpi.dwProcessId<<L"\n";
            CloseHandle(bpi.hThread);CloseHandle(bpi.hProcess);
        }else if(log)log<<L"Bridge start failed: "<<exe<<L" win32="<<GetLastError()<<L"\n";
    };
    if(!bhPipe.empty())startBridge(L"KharvoxBhapticsBridge.exe",{{L"KHARVOX_BHAPTICS_PIPE_NAME",bhPipe},{L"KHARVOX_BHAPTICS_SESSION_TOKEN",bhToken}});
    if(!psPipe.empty())startBridge(L"KharvoxPsvr2Bridge.exe",{{L"KHARVOX_USE_PSVR2_TOOLKIT",L"1"},{L"KHARVOX_PSVR2_PIPE_NAME",psPipe},{L"KHARVOX_PSVR2_SESSION_TOKEN",psToken}});
    if(log)log<<L"Eternal launched: PID "<<pi.dwProcessId<<L"\n"<<args<<L"\n";
    setStatus(L"DOOM Eternal launched; watching startup...");
    const DWORD early = WaitForSingleObject(pi.hProcess, 30000);
    if (early == WAIT_OBJECT_0) {
        DWORD code{};
        GetExitCodeProcess(pi.hProcess, &code);
        if (log) log << L"DOOM Eternal exited during startup. exitCode=0x" << std::hex << code << std::dec << L"\n";
        wchar_t status[128]{};
        swprintf_s(status, L"DOOM Eternal exited during startup. code=0x%08X", code);
        setStatus(status);
    } else {
        setStatus(L"DOOM Eternal launched.");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
