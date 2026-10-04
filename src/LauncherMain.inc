int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR arguments, int show) {
    constexpr wchar_t mouseRestoreArgument[]=L"--restore-mouse-handle ";
    if(arguments&&std::wstring(arguments).find(mouseRestoreArgument)==0)
        return argent::mouse::restoreAfterSession(arguments+wcslen(mouseRestoreArgument));
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    root = std::filesystem::path(path).parent_path();
    if(arguments&&std::wstring(arguments)==L"--intro-self-test")return 0;
    if(arguments&&std::wstring(arguments)==L"--intro-self-test-fail")return 1;
    if(arguments&&std::wstring(arguments)==L"--vr-intro")return runArgentVrIntro((root/L"openxr_loader.dll").c_str());
    const bool selfTest=arguments&&std::wstring(arguments)==L"--self-test";
    isolatedSettings=selfTest;
    if(selfTest){root=std::filesystem::temp_directory_path()/(L"ArgentLauncher-Test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/L"assets");
      std::ofstream(root/L"assets/argent_controls.cfg")<<"hand left 0.1 0 0 0 0 0\nweapon default 0 0 0 0 0 0 0 0 -0.3 0.14 1 1\n";}
    isolatedSettingsRoot=root;


    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    auto logoPath = root / L"assets" / L"kharvox-argent-logo.png";
    if (!std::filesystem::exists(logoPath)) logoPath = root.parent_path() / L"assets" / L"kharvox-argent-logo.png";
    if (std::filesystem::exists(logoPath)) logo = Gdiplus::Image::FromFile(logoPath.c_str(), FALSE);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    backgroundBrush = CreateSolidBrush(Background);
    panelBrush = CreateSolidBrush(Panel);
    fieldBrush = CreateSolidBrush(Field);

    WNDCLASSW pageClass{};
    pageClass.lpfnWndProc = pageProc;
    pageClass.hInstance = instance;
    pageClass.lpszClassName = L"ArgentTabPage";
    pageClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pageClass.hbrBackground = backgroundBrush;
    RegisterClassW(&pageClass);
    pageClass.lpfnWndProc=helpProc;pageClass.lpszClassName=L"ArgentHelpPage";RegisterClassW(&pageClass);
    auto bindingPath=root/L"assets/argent-controller-bindings.png";
    if(!std::filesystem::exists(bindingPath))bindingPath=std::filesystem::path(__FILE__).parent_path().parent_path()/L"assets/argent-controller-bindings.png";
    if(std::filesystem::exists(bindingPath))controllerBindingImage=Gdiplus::Image::FromFile(bindingPath.c_str(),FALSE);

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ArgentLauncher";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(201),IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    wc.hbrBackground = backgroundBrush;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"KHARVOX: ARGENT Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 916, 760, nullptr, nullptr, instance, nullptr);
    SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(wc.hIcon));
    SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,
        MAKEINTRESOURCEW(201),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));
    if(selfTest){
      settingsReady=false;
      int result=0;
      try{
        auto checked=[](bool b,int line){if(!b)throw std::runtime_error("launcher integration test failed at line "+std::to_string(line));};
#define require(...) checked((__VA_ARGS__),__LINE__)
        {
            const auto scaleBefore=selected(renderScale);const bool fsrBefore=isChecked(fsr);
            updateRenderingAvailability(true);
            require(!IsWindowEnabled(renderScale)&&!IsWindowEnabled(fsr));
            wchar_t hint[256]{};GetWindowTextW(fsrStatus,hint,256);
            require(std::wstring(hint)==L"SteamVR detected , adjust render scale within SteamVR.");
            require(selected(renderScale)==scaleBefore&&isChecked(fsr)==fsrBefore);
            updateRenderingAvailability(false);
            require(IsWindowEnabled(renderScale)&&IsWindowEnabled(fsr));
            GetWindowTextW(fsrStatus,hint,256);
            require(std::wstring(hint)==L"FSR is disabled at 100% res or higher. Turn off for DLSS usage.");
        }
        {
            std::string config="// in_mouse \"0\"\r\nin_mouse_speed \"0\"\r\nin_mouse \"0\"\r\nother \"7\"\r\n";
            require(argent::mouse::enableSavedMouse(config));
            require(config=="// in_mouse \"0\"\r\nin_mouse_speed \"0\"\r\nin_mouse \"1\"\r\nother \"7\"\r\n");
            require(!argent::mouse::enableSavedMouse(config));
            std::string utf16="\xff\xfe";
            for(char c:std::string("in_mouse \"0\"\r\n")){utf16+=c;utf16+='\0';}
            require(argent::mouse::enableSavedMouse(utf16));
            require(utf16[22]=='1'&&utf16[23]=='\0');
            std::string plain="in_mouse 0";require(argent::mouse::enableSavedMouse(plain)&&plain=="in_mouse 1");
            std::string bom="\xef\xbb\xbfin_mouse \"0\"";
            require(argent::mouse::enableSavedMouse(bom)&&bom=="\xef\xbb\xbfin_mouse \"1\"");
        }
        require(SendMessageW(hwnd,WM_GETICON,ICON_BIG,0)&&SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0));
        for(int size:{16,20,24,32,40,48,64,128,256}){
            auto icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(201),IMAGE_ICON,size,size,0));
            require(icon!=nullptr);ICONINFO info{};require(GetIconInfo(icon,&info));
            BITMAP bitmap{};require(GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap));
            require(bitmap.bmWidth==size&&bitmap.bmHeight==size);
            DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);DestroyIcon(icon);
        }
        require(!advancedTabs&&!(GetWindowLongPtrW(tabButtons[3],GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongPtrW(tabButtons[4],GWL_STYLE)&WS_VISIBLE));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdCredit,BN_CLICKED),0);require(!advancedTabs);
        BYTE keyboard[256]{};GetKeyboardState(keyboard);BYTE shifted[256];CopyMemory(shifted,keyboard,256);shifted[VK_SHIFT]|=0x80;SetKeyboardState(shifted);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdCredit,BN_CLICKED),0);SetKeyboardState(keyboard);require(advancedTabs);
        showTab(4);layoutTabs(false);require(activeTab==0);
        for(int i=5;i<7;++i){showTab(i);SendMessageW(tabPages[i],WM_KEYDOWN,VK_END,0);require(helpStates[i-5].scroll>0);SendMessageW(tabPages[i],WM_KEYDOWN,VK_HOME,0);require(helpStates[i-5].scroll==0);}showTab(0);
        require(controllerBindingImage&&controllerBindingImage->GetLastStatus()==Gdiplus::Ok);
        // Render the actual native help layout as an inspection artifact.
        for(int i=0;i<2;++i){
            HDC dc=GetDC(hwnd);HDC memory=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,773,900);auto old=SelectObject(memory,bitmap);
            RECT area{0,0,773,900};FillRect(memory,&area,backgroundBrush);paintHelp(memory,773,helpStates[i],true);
            {Gdiplus::Bitmap output(bitmap,nullptr);CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
             output.Save((std::filesystem::current_path()/(i?L"launcher-instructions-preview.png":L"launcher-bindings-preview.png")).c_str(),&encoder,nullptr);}
            SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(hwnd,dc);
        }
        require(GetParent(desktopMirror)==tabPages[0]&&!isChecked(desktopMirror));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdDesktopMirror,BN_CLICKED),reinterpret_cast<LPARAM>(desktopMirror));require(isChecked(desktopMirror));
        SendMessageW(mirrorResolution,CB_SETCURSEL,3,0);
        saveSettings();setChecked(desktopMirror,false);SendMessageW(mirrorResolution,CB_SETCURSEL,0,0);loadSettings();require(isChecked(desktopMirror)&&selected(mirrorResolution)==3);
        SendMessageW(mirrorResolution,CB_SETCURSEL,1,0);
        setChecked(desktopMirror,false);saveSettings();
        require(!isChecked(renderingLogging)&&!isChecked(extendedLogging));
        require(GetParent(captureShaders)==tabPages[3]&&!isChecked(captureShaders));
        setChecked(renderingLogging,true);require(isChecked(extendedLogging)&&!isChecked(captureShaders));
        setChecked(extendedLogging,false);require(!isChecked(renderingLogging));

        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdRenderingLogging,BN_CLICKED),reinterpret_cast<LPARAM>(renderingLogging));require(isChecked(extendedLogging)&&isChecked(renderingLogging));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdExtendedLogging,BN_CLICKED),reinterpret_cast<LPARAM>(extendedLogging));require(!isChecked(extendedLogging)&&!isChecked(renderingLogging));
        require(GetParent(disableVrIntro)==tabPages[0]&&!vrIntroStarted&&!isChecked(disableVrIntro));
        require(!(GetWindowLongPtrW(disableVrIntro,GWL_STYLE)&WS_VISIBLE));
        for(int id:{101,102,104,105,106,107})require(FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA)!=nullptr);
        {std::wofstream introLog(root/L"intro-test.log");settingsReady=true;
         require(!playNativeVrIntro(introLog,L"--intro-self-test-fail"));
         require(vrIntroStarted&&(GetWindowLongPtrW(disableVrIntro,GWL_STYLE)&WS_VISIBLE)&&!introThreadId);
         require(playNativeVrIntro(introLog,L"--intro-self-test"));
         SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdDisableVrIntro,BN_CLICKED),reinterpret_cast<LPARAM>(disableVrIntro));
         require(isChecked(disableVrIntro));
         vrIntroStarted=false;setChecked(disableVrIntro,false);loadSettings();
         require(vrIntroStarted&&isChecked(disableVrIntro));
         require(playNativeVrIntro(introLog,L"--intro-self-test-fail")); // Disabled: no child launched.
         settingsReady=false;
        }
        {
            std::map<std::wstring,std::wstring> env{{L"ARGENT_CAPTURE_DIRECTORY",L"inherited"}};
            const auto package=root/L"capture-test";
            require(configureShaderCapture(env,false,package,L"off").empty());
            require(env.at(L"ARGENT_CAPTURE_DIRECTORY").empty()&&!std::filesystem::exists(package));
            require(env.at(L"ARGENT_WATER_GPU_CAPTURE")==L"0");
            std::filesystem::create_directories(package);
            std::ofstream(package/L"build-info.json")<<"{\"release\":\"test\"}";
            const auto capture=configureShaderCapture(env,true,package,L"on");
            require(env.at(L"ARGENT_CAPTURE_DIRECTORY")==capture.wstring());
            require(env.at(L"ARGENT_WATER_GPU_CAPTURE")==L"1"&&env.at(L"ARGENT_DECAL_GPU_CAPTURE")==L"0");
            require(std::filesystem::exists(capture/L"capture-session.txt")&&std::filesystem::exists(capture/L"build-info.json"));
            bool rejected=false;
            try{configureShaderCapture(env,true,package,L"on");}catch(const std::exception&){rejected=true;}
            require(rejected&&env.at(L"ARGENT_CAPTURE_DIRECTORY").empty());
            const auto blocked=root/L"capture-blocked";
            std::filesystem::create_directories(blocked);std::ofstream(blocked/L"captures").put('x');
            rejected=false;
            try{configureShaderCapture(env,true,blocked,L"failed");}catch(const std::exception&){rejected=true;}
            require(rejected&&env.at(L"ARGENT_CAPTURE_DIRECTORY").empty());
            configureShaderCapture(env,false,package,L"off-again");
            require(env.at(L"ARGENT_CAPTURE_DIRECTORY").empty());
        }
        for(int mode:{0,1,2})for(const auto* inherited:{L"0",L"1"}){
            std::map<std::wstring,std::wstring> env{{L"ARGENT_DLSS_STEREO",inherited},{L"XR_RUNTIME_JSON",L"steamxr_win64.json"}};
            configureStereoDlss(env,mode);
            require(env.at(L"ARGENT_DLSS_STEREO")== (mode==1?L"1":L"0"));
            require(env.at(L"XR_RUNTIME_JSON")==L"steamxr_win64.json");
        }
        require(launcherSettingsPath().parent_path()==root/L"local-settings");
        const auto fixture=root/L"test-install/DOOMEternalx64vk.exe";
        std::filesystem::create_directories(fixture.parent_path());std::ofstream(fixture).put('x');
        require(!microsoftStoreGame(fixture));
        {std::ofstream config(fixture.parent_path()/L"MicrosoftGame.Config");config<<"<Identity Name=\"BethesdaSoftworks.DOOMEternal-PC\"/>";}
        require(microsoftStoreGame(fixture));
        std::filesystem::remove(fixture.parent_path()/L"MicrosoftGame.Config");
        const auto xboxFixture=root/L"XboxGames/Arbitrary Install Name/Content/DOOMEternalx64vk.exe";
        std::filesystem::create_directories(xboxFixture.parent_path());std::ofstream(xboxFixture).put('x');
        {std::ofstream config(xboxFixture.parent_path()/L"MicrosoftGame.Config");config<<"<Identity Name=\"BethesdaSoftworks.DOOMEternal-PC\"/>";}
        require(findMicrosoftStoreGame(root/L"XboxGames")==xboxFixture);
        const auto portableIni=(root/L"assets/launcher.ini").wstring();
        WritePrivateProfileStringW(L"launcher",L"game",fixture.c_str(),portableIni.c_str());
        loadSettings();wchar_t selectedPath[32768]{};GetWindowTextW(gameEdit,selectedPath,32768);
        require(std::wstring(selectedPath)!=fixture.wstring()); // Never import another tester's path.
        SetWindowTextW(gameEdit,fixture.c_str());writeControlsConfig();SetWindowTextW(gameEdit,L"");loadSettings();
        GetWindowTextW(gameEdit,selectedPath,32768);require(std::wstring(selectedPath)==fixture.wstring());
        std::filesystem::remove(fixture);loadSettings();GetWindowTextW(gameEdit,selectedPath,32768);
        require(std::wstring(selectedPath)!=fixture.wstring()); // Stale saved paths trigger discovery.
        require(GetParent(extendedLogging)==tabPages[3]&&!isChecked(extendedLogging)&&loggingArgument().empty());
        require(gpuDiagnostics&&GetParent(gpuDiagnostics)==tabPages[3]&&!isChecked(gpuDiagnostics));
        std::map<std::wstring,std::wstring> diagnosticEnv;
        setChecked(extendedLogging,true);configureGpuDiagnostics(diagnosticEnv,isChecked(gpuDiagnostics));require(diagnosticEnv[L"ARGENT_GPU_DIAGNOSTICS"]==L"0");
        setChecked(extendedLogging,false);configureGpuDiagnostics(diagnosticEnv,true);require(diagnosticEnv[L"ARGENT_GPU_DIAGNOSTICS"]==L"1");
        if(cleanRelease)require(captureShaders&&!disableAa&&!simulator&&tabButtons[3]);
        require(GetParent(cinematics3d)==tabPages[0]&&!isChecked(cinematics3d));
        wchar_t cinematicLabel[32]{};GetWindowTextW(cinematics3d,cinematicLabel,32);require(std::wstring(cinematicLabel)==L"3D Cinematics");
        wchar_t label[64]{};GetWindowTextW(renderScale,label,64);require(std::wstring(label)==L"100 %");
        GetWindowTextW(extendedLogging,label,64);require(std::wstring(label)==L"Extended Logging");
        auto checkRange=[&](HWND combo,int minimum,int maximum){
          COMBOBOXINFO info{sizeof(info)};require(GetComboBoxInfo(combo,&info)!=FALSE);
          require((GetWindowLongPtrW(info.hwndList,GWL_STYLE)&WS_VSCROLL)!=0);
          require(IsWindowEnabled(combo));
          require(SendMessageW(combo,CB_GETCOUNT,0,0)==(maximum-minimum)/5+1);
          for(int i=0;i<=(maximum-minimum)/5;++i){wchar_t text[32]{};SendMessageW(combo,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(text));require(_wtoi(text)==minimum+i*5);}
        };
        require(GetParent(calibrationMode)==tabPages[4]&&isChecked(showHands));
        require(calibrationProfiles.back()=="sentinel_hammer"&&SendMessageW(calibrationProfile,CB_GETCOUNT,0,0)==calibrationProfiles.size());
        require(GetParent(handSmoothing)==tabPages[2]&&!isChecked(handSmoothing));
        setChecked(handSmoothing,true);writeControlsConfig();setChecked(handSmoothing,false);loadSettings();require(isChecked(handSmoothing));
        setChecked(handSmoothing,false);writeControlsConfig();
        require(GetParent(handsJump)==tabPages[2]&&isChecked(handsJump));
        setChecked(handsJump,false);writeControlsConfig();setChecked(handsJump,true);loadSettings();require(!isChecked(handsJump));
        setChecked(handsJump,true);writeControlsConfig();
        {std::ofstream stale(root/L"assets/argent_controls.cfg",std::ios::app);stale<<"\nlaser_sight 1\n";}
        loadSettings();require(controlsSettings().at("laser_sight")=="0");writeControlsConfig();
        for(auto pivot:weaponPivot)require(GetParent(pivot)==tabPages[4]&&SendMessageW(pivot,CB_GETCOUNT,0,0)==121);
        SendMessageW(weaponPivot[0],CB_SETCURSEL,40,0);SendMessageW(weaponPivot[1],CB_SETCURSEL,65,0);SendMessageW(weaponPivot[2],CB_SETCURSEL,57,0);
        writeControlsConfig();for(auto pivot:weaponPivot)SendMessageW(pivot,CB_SETCURSEL,60,0);
        loadSettings();require(selected(weaponPivot[0])==40&&selected(weaponPivot[1])==65&&selected(weaponPivot[2])==57);
        SendMessageW(weaponPivot[1],CB_SETCURSEL,60,0);SendMessageW(weaponPivot[2],CB_SETCURSEL,60,0);

        SendMessageW(calibrationMode,CB_SETCURSEL,1,0);SendMessageW(calibrationProfile,CB_SETCURSEL,1,0);
        writeControlsConfig();SendMessageW(calibrationMode,CB_SETCURSEL,0,0);loadSettings();require(selected(calibrationMode)==1&&selected(calibrationProfile)==1);
        SendMessageW(calibrationMode,CB_SETCURSEL,0,0);SendMessageW(calibrationProfile,CB_SETCURSEL,0,0);writeControlsConfig();
        // Selecting a target starts preview; APPLY snapshots its identity and
        // reports success only after the runtime acknowledges the request.
        SendMessageW(calibrationMode,CB_SETCURSEL,2,0);SendMessageW(calibrationProfile,CB_SETCURSEL,4,0);
        setChecked(dominantHand,false);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdApplyCalibration,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(tabPages[4],IdApplyCalibration)));
        require(calibrationApplyWaiting&&calibrationApplyCommand.mode=="weapon"&&calibrationApplyCommand.profile=="rocket_launcher"&&!calibrationApplyCommand.left);
        require(controlsSettings().at("calibration_apply_profile")=="rocket_launcher");
        {std::ofstream ack(root/L"calibration_apply_status.txt");ack<<calibrationApplyCommand.revision<<" 1 weapon rocket_launcher_right";}
        SendMessageW(hwnd,WM_TIMER,1,0);require(!calibrationApplyWaiting);
        SendMessageW(calibrationMode,CB_SETCURSEL,0,0);SendMessageW(calibrationProfile,CB_SETCURSEL,0,0);writeControlsConfig();
        checkRange(renderScale,40,200);checkRange(smoothSpeed,200,300);checkRange(snapAngle,45,90);
        const auto testIni=launcherSettingsPath().wstring();
        WritePrivateProfileStringW(L"launcher",L"renderScalePercent",nullptr,testIni.c_str());
        WritePrivateProfileStringW(L"launcher",L"renderScale",L"3",testIni.c_str());loadSettings();
        GetWindowTextW(renderScale,label,64);require(std::wstring(label)==L"80 %");

        SendMessageW(renderScale,CB_SETCURSEL,2,0);setChecked(fsr,true);
        SendMessageW(turnMode,CB_SETCURSEL,0,0);SendMessageW(smoothSpeed,CB_SETCURSEL,20,0);
        setChecked(dominantHand,true);SendMessageW(killSpeed,CB_SETCURSEL,9,0);SendMessageW(killHands,CB_SETCURSEL,0,0);setChecked(physicalKill,true);
        writeControlsConfig();
        std::ifstream config(root/L"assets/argent_controls.cfg");std::string content((std::istreambuf_iterator<char>(config)),{});config.close();
        for(auto expected:{"hand left 0.1 0 0 0 0 0","dominant left","turn smooth","smooth_turn_speed 300","physical_glory_kill_speed 2.8","physical_glory_kill_hands left","crouch_enabled 0","automatic_presentation 1","cinematics_quad 1","sync_immersive 1","movement_immersive 1","shoulder_chainsaw 1","virtual_gunstock 1","bhaptics_enabled 0","psvr2_adaptive_triggers 0"})require(content.find(expected)!=std::string::npos);
        setChecked(fsr,false);SendMessageW(renderScale,CB_SETCURSEL,5,0);setChecked(dominantHand,false);loadSettings();
        require(isChecked(fsr)&&selected(renderScale)==2&&isChecked(dominantHand)&&selected(smoothSpeed)==20&&selected(killHands)==0);
        require(GetParent(leftHandSwap)==tabPages[1]&&IsWindowEnabled(leftHandSwap)&&selected(leftHandSwap)==0&&(GetWindowLongPtrW(leftHandSwap,GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongPtrW(leftHandLayoutLabel,GWL_STYLE)&WS_VISIBLE));
        SendMessageW(leftHandSwap,CB_SETCURSEL,1,0);writeControlsConfig();SendMessageW(leftHandSwap,CB_SETCURSEL,0,0);loadSettings();require(selected(leftHandSwap)==1);
        setChecked(dominantHand,false);writeControlsConfig();loadSettings();require(!IsWindowEnabled(leftHandSwap)&&selected(leftHandSwap)==1&&!(GetWindowLongPtrW(leftHandSwap,GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongPtrW(leftHandLayoutLabel,GWL_STYLE)&WS_VISIBLE));
        setChecked(dominantHand,true);SendMessageW(leftHandSwap,CB_SETCURSEL,0,0);writeControlsConfig();loadSettings();
        SendMessageW(turnMode,CB_SETCURSEL,1,0);SendMessageW(snapAngle,CB_SETCURSEL,9,0);writeControlsConfig();SendMessageW(snapAngle,CB_SETCURSEL,0,0);loadSettings();require(selected(snapAngle)==9&&selected(turnMode)==1);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdExtendedLogging,BN_CLICKED),reinterpret_cast<LPARAM>(extendedLogging));
        require(isChecked(extendedLogging)&&loggingArgument()==L" -ExtendedLogging");
        writeControlsConfig();setChecked(extendedLogging,false);loadSettings();require(isChecked(extendedLogging));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdExtendedLogging,BN_CLICKED),reinterpret_cast<LPARAM>(extendedLogging));
        writeControlsConfig();setChecked(extendedLogging,true);loadSettings();require(!isChecked(extendedLogging)&&loggingArgument().empty());
        settingsReady=true;
        // Real notifications must persist every control without PLAY or APPLY.
        for(auto control:{dominantHand,handSmoothing,handsJump,cinematics3d,fsr,physicalKill,virtualGunstock,bhaptics,psvr2Triggers,extendedLogging,gpuDiagnostics,showHands,disableAa,captureShaders,simulator}){
            if(!control)continue;
            for(int repeat=0;repeat<2;++repeat){
                const bool expected=!isChecked(control);
                SendMessageW(GetParent(control),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(control),BN_CLICKED),reinterpret_cast<LPARAM>(control));
                setChecked(control,!expected);loadSettings();require(isChecked(control)==expected);
            }
        }
        for(auto control:{renderScale,turnMode,snapAngle,movement,leftHandSwap,smoothSpeed,killSpeed,killHands,calibrationMode,calibrationProfile,weaponPivot[0],weaponPivot[1],weaponPivot[2]}){
            const int expected=(selected(control)+1)%int(SendMessageW(control,CB_GETCOUNT,0,0));
            SendMessageW(control,CB_SETCURSEL,expected,0);
            SendMessageW(GetParent(control),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(control),CBN_SELCHANGE),reinterpret_cast<LPARAM>(control));
            SendMessageW(control,CB_SETCURSEL,0,0);loadSettings();require(selected(control)==expected);
        }
        std::ofstream(fixture).put('x');SetWindowTextW(gameEdit,fixture.c_str());
        settingsReady=false;SetWindowTextW(gameEdit,L"");settingsReady=true;loadSettings();
        GetWindowTextW(gameEdit,selectedPath,32768);require(std::wstring(selectedPath)==fixture.wstring());
        const auto expectedControls=controlsSettings();const int expectedScale=selected(renderScale);
        const auto originalRoot=root;
        root=originalRoot/L"release-switch/ARGENT-Alpha-Test-r103";std::filesystem::create_directories(root/L"assets");
        std::ofstream(root/L"assets/argent_controls.cfg")<<"dominant right\nturn snap 45\nbhaptics_enabled 0\nshow_hands 0\n";
        const auto sharedIni=launcherSettingsPath();require(sharedIni==originalRoot/L"local-settings/launcher.ini");
        loadSettings();require(controlsSettings()==expectedControls&&selected(renderScale)==expectedScale);
        writeControlsConfig();
        // Migrate only the old release saved together with the shared INI.
        WritePrivateProfileStringW(L"controls",nullptr,nullptr,sharedIni.c_str());
        const auto prior=root.parent_path()/L"ARGENT-Alpha-Test-r102/assets/argent_controls.cfg";
        std::filesystem::create_directories(prior.parent_path());std::ofstream(prior)<<"dominant left\nbhaptics_enabled 1\n";
        std::filesystem::last_write_time(prior,std::filesystem::last_write_time(sharedIni));
        require(legacyControlsPath()==prior);loadSettings();require(isChecked(dominantHand)&&isChecked(bhaptics));
        std::filesystem::last_write_time(prior,std::filesystem::last_write_time(sharedIni)-std::chrono::minutes(1));
        std::filesystem::last_write_time(root/L"assets/argent_controls.cfg",std::filesystem::last_write_time(sharedIni)-std::chrono::minutes(1));
        require(legacyControlsPath()==root/L"assets/argent_controls.cfg");
        saveSettings();
        WritePrivateProfileStringW(L"future",L"keep",L"unchanged",sharedIni.c_str());
        const HANDLE locked=CreateFileW(sharedIni.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(locked!=INVALID_HANDLE_VALUE);const bool blockedSave=autoSaveSettings();CloseHandle(locked);require(!blockedSave);
        require(autoSaveSettings());wchar_t retained[32]{};
        GetPrivateProfileStringW(L"future",L"keep",L"",retained,32,sharedIni.c_str());require(std::wstring(retained)==L"unchanged");
        // Closing also saves state that did not issue a notification.
        WritePrivateProfileStringW(L"launcher",L"renderer",L"0",sharedIni.c_str());
        SendMessageW(hwnd,WM_CLOSE,0,0);
        require(!IsWindow(hwnd)&&GetPrivateProfileIntW(L"launcher",L"renderer",-1,sharedIni.c_str())==1);
#undef require
      }catch(const std::exception& error){std::ofstream(isolatedSettingsRoot/L"selftest-failure.txt")<<error.what();result=1;}
       catch(...){result=1;}
      if(IsWindow(hwnd))DestroyWindow(hwnd);return result;
    }
    if(arguments&&std::wstring(arguments)==L"--startup-check"){
        runLauncher(true);
        DestroyWindow(hwnd);
        return 0;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    if (!argent::mouse::gameRunning()&&!argent::mouse::repairSavedMouse())
        setStatus(L"Cannot restore mouse settings. Check access to the DOOM Eternal Saved Games folder.");
    if (runningUnderWine()) {
        MessageBoxW(hwnd,
            L"Linux is not supported! Give it a try but Windows 11 or Temple OS is recommended!",
            L"ARGENT - Compatibility notice", MB_OK | MB_ICONINFORMATION);
    }
    if (hardwareGpuSchedulingEnabled()) {
        MessageBoxW(hwnd,
            L"Hardware-accelerated GPU scheduling (HAGS) is enabled.\n\n"
            L"We recommend turning it off when playing ARGENT.\n\n"
            L"Open Windows Settings > System > Display > Graphics and turn off "
            L"Hardware-accelerated GPU scheduling in the default or advanced graphics settings. "
            L"Restart your PC for the change to take effect.\n\n"
            L"You can continue using ARGENT with HAGS enabled.",
            L"ARGENT - GPU scheduling recommendation", MB_OK | MB_ICONINFORMATION);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
