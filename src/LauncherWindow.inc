void showTab(int index) {
    if(index<0||index>=7||(!advancedTabs&&(index==3||index==4)))return;
    activeTab = index;
    if(index>=5)SetFocus(tabPages[index]);
    for (int i = 0; i < 7; ++i) ShowWindow(tabPages[i], i == index ? SW_SHOW : SW_HIDE);
    for (auto button : tabButtons) if (button) InvalidateRect(button, nullptr, TRUE);
}

void layoutTabs(bool advanced) {
    advancedTabs=advanced;
    if(!advanced&&(activeTab==3||activeTab==4))showTab(0);
    int x=42;
    for(int i:{0,1,2,5,6,3,4}){
        const bool visible=advanced||i<3||i>4;
        ShowWindow(tabButtons[i],visible?SW_SHOW:SW_HIDE);
        if(visible){const int width=advanced?(i==5?162:109):(i==5?216:150);MoveWindow(tabButtons[i],x,212,width,40,TRUE);x+=width;}
    }
}
LRESULT CALLBACK pageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetParent(hwnd), msg, wp, lp);
    case WM_ERASEBKGND: {
        RECT area{}; GetClientRect(hwnd, &area);
        FillRect(reinterpret_cast<HDC>(wp), &area, backgroundBrush);
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void createTabs(HWND hwnd) {
    const wchar_t* labels[]{ L"Rendering", L"Movement", L"VR Options", L"Development", L"Calibration", L"Controller Binding", L"Instructions" };
    for (int i = 0; i < 7; ++i)
        tabButtons[i] = addControl(hwnd, L"BUTTON", labels[i], BS_OWNERDRAW | WS_TABSTOP,
            42 + i * 163, 212, 163, 40, IdTabBase + i);
    for (int i = 0; i < 5; ++i) {
        tabPages[i] = CreateWindowW(L"ArgentTabPage", L"", WS_CHILD | WS_VISIBLE, 55, 264, 790, 264,
            hwnd, nullptr, nullptr, nullptr);
        SendMessageW(tabPages[i], WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    }

    for(int i=5;i<7;++i)tabPages[i]=CreateWindowW(L"ArgentHelpPage",L"",WS_CHILD|WS_VSCROLL|WS_TABSTOP,55,264,790,264,hwnd,nullptr,nullptr,&helpStates[i-5]);
    layoutTabs(false);
    addLabel(tabPages[0], L"RenderScale", 24, 24, 190, 24);
    renderScale = addCombo(tabPages[0], 250, 18, 180, 180, IdRenderScale,
        {}, 0);
    for(int v=40;v<=200;v+=5){auto t=std::to_wstring(v)+L" %";SendMessageW(renderScale,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
    SendMessageW(renderScale,CB_SETCURSEL,12,0);
    fsrStatus=addLabel(tabPages[0], L"FSR is disabled at 100% res or higher. Turn off for DLSS usage.", 24, 58, 730, 24, true);

    fsr=addCheck(tabPages[0],L"FSR Upscaling",450,16,315,32,IdFsr);
    disableVrIntro=addCheck(tabPages[0],L"Disable VR Intro",395,98,330,32,IdDisableVrIntro);
    ShowWindow(disableVrIntro,SW_HIDE);
    cinematics3d=addCheck(tabPages[0],L"3D Cinematics",24,98,330,32,IdCinematics3d);
    renderingLogging=addCheck(tabPages[0],L"Extended Logging",24,138,330,32,IdRenderingLogging);
    desktopMirror=addCheck(tabPages[0],L"Desktop Mirror (Right Eye)",24,178,360,32,IdDesktopMirror);
    mirrorResolution=addCombo(tabPages[0],420,178,320,160,IdMirrorResolution,{L"720p (1280 x 720)",L"1080p (1920 x 1080)",L"2K / 1440p (2560 x 1440)",L"4K (3840 x 2160)"},1);
    addLabel(tabPages[0],L"Shows one eye in the game window at up to 60 FPS. Applies on next game start.",24,216,730,24,true);

    addLabel(tabPages[1], L"Turning", 24, 18, 180, 24);
    turnMode = addCombo(tabPages[1], 250, 14, 230, 160, IdTurnMode, { L"Smooth", L"Snap" }, 1);
    addLabel(tabPages[1], L"Snap angle", 510, 18, 120, 24);
    snapAngle = addCombo(tabPages[1], 642, 14, 100, 160, IdSnapAngle,
        {},0);
    for(int v=45;v<=90;v+=5){auto t=std::to_wstring(v);SendMessageW(snapAngle,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
    SendMessageW(snapAngle,CB_SETCURSEL,0,0);
    addLabel(tabPages[1], L"Smooth speed (deg/s)",24,60,190,24);
    smoothSpeed=addCombo(tabPages[1],250,56,230,250,IdSmoothSpeed,{},0);
    for(int v=200;v<=300;v+=5){auto t=std::to_wstring(v);SendMessageW(smoothSpeed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
    SendMessageW(smoothSpeed,CB_SETCURSEL,6,0);
    addLabel(tabPages[1], L"Movement direction", 24, 102, 190, 24);
    movement = addCombo(tabPages[1], 250, 98, 300, 160, IdMovement, { L"Head direction", L"Off-hand direction" }, 0);
    dominantHand = addCheck(tabPages[1],L"Left Handed Mode",24,140,330,32,IdDominantHand);
    leftHandLayoutLabel=addLabel(tabPages[1],L"Left-hand layout",24,186,210,24);
    leftHandSwap=addCombo(tabPages[1],250,182,330,160,IdLeftHandSwap,{L"Button Swap",L"Button and Stick Swap"},0);
    updateLeftHandLayout();

    virtualGunstock=addCheck(tabPages[2],L"Virtual Gunstock",24,10,330,32,IdVirtualGunstock,true);
    bhaptics=addCheck(tabPages[2],L"bHaptics",395,10,330,32,IdBhaptics);
    psvr2Triggers=addCheck(tabPages[2],L"PSVR2 Adaptive Triggers",24,54,370,32,IdPsvr2Triggers);
    physicalKill=addCheck(tabPages[2],L"Physical Glory Kill / melee",24,98,375,32,IdPhysicalKill,true);
    addLabel(tabPages[2],L"Punch speed (m/s)",24,160,180,24);
    killSpeed=addCombo(tabPages[2],205,152,100,220,IdKillSpeed,{},0);
    for(int v=0;v<=15;++v){wchar_t t[16]{};swprintf_s(t,L"%.1f",1.+v*.2);SendMessageW(killSpeed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t));}
    SendMessageW(killSpeed,CB_SETCURSEL,9,0);
    addLabel(tabPages[2],L"Punch hand",395,160,130,24);
    killHands=addCombo(tabPages[2],540,152,210,150,IdKillHands,{L"Left",L"Right",L"Both"},2);
    handSmoothing=addCheck(tabPages[2],L"Hand Smoothing",395,54,330,32,IdHandSmoothing);
    handsJump=addCheck(tabPages[2],L"Hands Jump",395,98,330,32,IdHandsJump,true);

    if(cleanRelease){
    extendedLogging = addCheck(tabPages[3], L"Extended Logging", 24, 18, 350, 32, IdExtendedLogging);
    captureShaders = addCheck(tabPages[3], L"Capture shaders", 24, 60, 350, 32, IdCaptureShaders);
    gpuDiagnostics = addCheck(tabPages[3], L"GPU crash diagnostics", 430, 18, 330, 32, IdGpuDiagnostics);
    addLabel(tabPages[3], L"Extra driver instrumentation. May affect timing. Restart the game after changing.", 430, 56, 310, 54, true);
    addLabel(tabPages[3], L"Enable before PLAY. F8 captures water/depth pairs or both scene eyes when no water pass is active. Wait for completion tone; may stutter.", 24, 106, 380, 110, true);
    addControl(tabPages[3], L"BUTTON", L"RUN DIAGNOSTICS", BS_OWNERDRAW | WS_TABSTOP, 430, 116, 290, 42, IdDiagnose);
    addControl(tabPages[3], L"BUTTON", L"OPEN LOG FOLDER", BS_OWNERDRAW | WS_TABSTOP, 430, 174, 290, 42, IdLogs);
    }else{
    disableAa = addCheck(tabPages[3], L"Disable all AA (including DLSS)", 24, 18, 340, 32, IdDisableAa);
    captureShaders = addCheck(tabPages[3], L"Capture shaders", 24, 62, 300, 32, IdCaptureShaders);
    simulator = addCheck(tabPages[3], L"Use OpenXR simulator", 24, 106, 300, 32, IdSimulator);
    extendedLogging = addCheck(tabPages[3], L"Extended Logging", 24, 150, 350, 32, IdExtendedLogging);
    addLabel(tabPages[3], L"CPU/GPU timings in .\\logs; profiling may affect performance.", 24, 194, 730, 24, true);
    addControl(tabPages[3], L"BUTTON", L"RUN DIAGNOSTICS", BS_OWNERDRAW | WS_TABSTOP, 430, 18, 290, 42, IdDiagnose);
    addControl(tabPages[3], L"BUTTON", L"OPEN LOG FOLDER", BS_OWNERDRAW | WS_TABSTOP, 430, 78, 290, 42, IdLogs);
    gpuDiagnostics = addCheck(tabPages[3], L"GPU crash diagnostics", 430, 142, 330, 32, IdGpuDiagnostics);
    }
    showHands=addCheck(tabPages[4],L"Show VR hands",24,6,300,32,IdShowHands,true);
    addLabel(tabPages[4],L"Calibration",24,52,110,24);
    calibrationMode=addCombo(tabPages[4],140,46,225,170,IdCalibrationMode,{L"Off (gameplay)",L"Hands",L"Weapon pose",L"Support point",L"Offhand HUD (live)"},0);
    addLabel(tabPages[4],L"Edit profile",375,52,100,24);
    calibrationProfile=addCombo(tabPages[4],480,46,265,220,IdCalibrationProfile,{},0);
    for(const auto& key:calibrationProfiles){std::wstring text(key.begin(),key.end());SendMessageW(calibrationProfile,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    SendMessageW(calibrationProfile,CB_SETCURSEL,0,0);
    addControl(tabPages[4],L"BUTTON",L"APPLY",BS_OWNERDRAW|WS_TABSTOP,24,90,270,38,IdApplyCalibration);
    addControl(tabPages[4],L"BUTTON",L"OPEN SAVED FILES",BS_OWNERDRAW|WS_TABSTOP,320,90,260,38,IdCalibrationFiles);
    const wchar_t* pivotLabels[]{L"Pivot: back (-) / forward (+)",L"Pivot: right (-) / left (+)",L"Pivot: down (-) / up (+)"};
    for(int i=0;i<3;++i){
        addLabel(tabPages[4],pivotLabels[i],24+i*248,138,240,24,true);
        weaponPivot[i]=addCombo(tabPages[4],24+i*248,164,225,220,IdPivotForward+i,{},60);
        for(int step=-60;step<=60;++step){wchar_t text[32];swprintf_s(text,L"%+.1f cm",step*.5);SendMessageW(weaponPivot[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));}
        SendMessageW(weaponPivot[i],CB_SETCURSEL,60,0);
    }
    addLabel(tabPages[4],L"Weapon-local pivot, both hands. APPLY to update live. 0 cm = calibrated grip (-15 cm forward).",24,202,740,24,true);
    addLabel(tabPages[4],L"HUD live: Num0 selects (blink), Num+ position/rotation, Num . artwork pivot.",24,224,740,20,true);
    addLabel(tabPages[4],L"HUD: Num4/6, 8/2, 7/9 adjust; * / size; Shift fine; Num5 reset. Auto-save.",24,244,740,20,true);
    addLabel(tabPages[4],L"Hands / Weapon pose: numpad previews; APPLY saves this profile and handedness.",24,264,740,20,true);
    showTab(0);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        uiFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        titleFont = CreateFontW(-23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        addLabel(hwnd, L"A DOOM ETERNAL VR CONVERSION MOD", 42, 166, 650, 26, true);
        addLabel(hwnd, L"Game location", 42, 530, 180, 22);
        const auto gamePath = findGameExecutable();
        gameEdit = addControl(hwnd, L"EDIT", gamePath.c_str(),
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 42, 558, 650, 32, IdGamePath);
        addControl(hwnd, L"BUTTON", L"BROWSE", BS_OWNERDRAW | WS_TABSTOP, 708, 557, 150, 34, IdBrowse);
        createTabs(hwnd);
        loadSettings();
        updateRenderingAvailability(usesSteamVrRuntime());
        launchButton = addControl(hwnd, L"BUTTON", L"PLAY", BS_OWNERDRAW | WS_TABSTOP,
            350, 614, 200, 48, IdLaunch);
        statusText = addLabel(hwnd, L"Ready", 42, 676, 635, 28, true);
        addControl(hwnd,L"BUTTON",L"Made by Cactus",BS_OWNERDRAW|WS_TABSTOP,700,676,158,28,IdCredit);
        settingsReady=true;
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT banner{ 42, 18, 858, 158 };
        FillRect(hdc, &banner, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (logo && logo->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            const auto sourceWidth = logo->GetWidth();
            const auto sourceHeight = logo->GetHeight();
            g.DrawImage(logo, Gdiplus::Rect(banner.left, banner.top, banner.right - banner.left, banner.bottom - banner.top),
                0, sourceHeight / 4, sourceWidth, sourceHeight / 2, Gdiplus::UnitPixel);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, msg == WM_CTLCOLORSTATIC && GetWindowLongPtrW(reinterpret_cast<HWND>(lp), GWLP_USERDATA) ? Muted : Text);
        const bool input = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        SetBkColor(hdc, input ? Field : Panel);
        return reinterpret_cast<LRESULT>(input ? fieldBrush : panelBrush);
    }
    case WM_MEASUREITEM: {
        auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if(measure->CtlType==ODT_COMBOBOX){measure->itemHeight=28;return TRUE;}
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType == ODT_BUTTON) {
            wchar_t label[256]{}; GetWindowTextW(dis->hwndItem, label, 256);
            const int controlId = GetDlgCtrlID(dis->hwndItem);
            if (controlId >= IdTabBase && controlId < IdTabBase + 7) {
                const bool active = controlId - IdTabBase == activeTab;
                FillRect(dis->hDC, &dis->rcItem, backgroundBrush);
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, Text);
                DrawTextW(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (active) {
                    RECT line = dis->rcItem; line.top = line.bottom - 3;
                    HBRUSH accentBrush = CreateSolidBrush(Accent);
                    FillRect(dis->hDC, &line, accentBrush); DeleteObject(accentBrush);
                }
                return TRUE;
            }
            if(controlId==IdCredit){FillRect(dis->hDC,&dis->rcItem,backgroundBrush);SetBkMode(dis->hDC,TRANSPARENT);SetTextColor(dis->hDC,Muted);DrawTextW(dis->hDC,label,-1,&dis->rcItem,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);if(dis->itemState&ODS_FOCUS)DrawFocusRect(dis->hDC,&dis->rcItem);return TRUE;}
            const bool check = dis->hwndItem == disableAa || dis->hwndItem == captureShaders ||
                dis->hwndItem == simulator || extraCheck(dis->hwndItem);
            HBRUSH brush = CreateSolidBrush(check ? Panel : ((dis->itemState & ODS_SELECTED) ? RGB(210, 45, 8) : Accent));
            FillRect(dis->hDC, &dis->rcItem, brush); DeleteObject(brush);
            const bool disabled=!IsWindowEnabled(dis->hwndItem);
            SetBkMode(dis->hDC, TRANSPARENT); SetTextColor(dis->hDC, disabled?Muted:Text);
            RECT textRect = dis->rcItem;
            if (check) {
                RECT box{ textRect.left + 2, textRect.top + 6, textRect.left + 22, textRect.top + 26 };
                HBRUSH boxBrush = CreateSolidBrush(isChecked(dis->hwndItem) ? (disabled?Muted:Accent) : Field);
                FillRect(dis->hDC, &box, boxBrush); DeleteObject(boxBrush);
                HBRUSH borderBrush = CreateSolidBrush(isChecked(dis->hwndItem) ? (disabled?Muted:Accent) : Border);
                FrameRect(dis->hDC, &box, borderBrush); DeleteObject(borderBrush);
                if (isChecked(dis->hwndItem)) {
                    HPEN pen = CreatePen(PS_SOLID, 2, Text);
                    HGDIOBJ previous = SelectObject(dis->hDC, pen);
                    MoveToEx(dis->hDC, box.left + 4, box.top + 10, nullptr);
                    LineTo(dis->hDC, box.left + 9, box.top + 15);
                    LineTo(dis->hDC, box.left + 17, box.top + 5);
                    SelectObject(dis->hDC, previous); DeleteObject(pen);
                }
                textRect.left += 34;
                DrawTextW(dis->hDC, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else DrawTextW(dis->hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        if (dis->CtlType == ODT_COMBOBOX) {
            HBRUSH brush = CreateSolidBrush(Field); FillRect(dis->hDC, &dis->rcItem, brush); DeleteObject(brush);
            if (dis->itemID != static_cast<UINT>(-1)) {
                wchar_t label[256]{}; SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(label));
                SetBkMode(dis->hDC, TRANSPARENT); SetTextColor(dis->hDC, IsWindowEnabled(dis->hwndItem)?Text:Muted);
                RECT r = dis->rcItem; r.left += 10; DrawTextW(dis->hDC, label, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                if (dis->itemState & ODS_SELECTED) {
                    RECT line = dis->rcItem; line.right = line.left + 3;
                    HBRUSH accentBrush = CreateSolidBrush(Accent);
                    FillRect(dis->hDC, &line, accentBrush); DeleteObject(accentBrush);
                }
            }
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if(introThreadId)return 0;
        if(LOWORD(wp)==IdCredit){if(HIWORD(wp)==BN_CLICKED&&(GetKeyState(VK_SHIFT)&0x8000))layoutTabs(!advancedTabs);return 0;}
        if (LOWORD(wp) >= IdTabBase && LOWORD(wp) < IdTabBase + 7) {
            showTab(LOWORD(wp) - IdTabBase);
            return 0;
        }
        if (HIWORD(wp) == CBN_SELCHANGE) {
            EnableWindow(snapAngle,TRUE);EnableWindow(smoothSpeed,TRUE);updateLeftHandLayout();
            autoSaveSettings();
            if(reinterpret_cast<HWND>(lp)==calibrationMode||reinterpret_cast<HWND>(lp)==calibrationProfile||reinterpret_cast<HWND>(lp)==dominantHand){
                try{writeControlsConfig();setStatus(L"Calibration target selected. Adjust in game, then APPLY to save.");}
                catch(...){setStatus(L"Cannot update calibration target.");}
            }
            return 0;
        }
        if(LOWORD(wp)==IdGamePath&&HIWORD(wp)==EN_CHANGE){autoSaveSettings();return 0;}
        if (HIWORD(wp) == BN_CLICKED) {
            HWND control = reinterpret_cast<HWND>(lp);
            const bool check = control == disableAa || control == captureShaders || control == simulator ||
                extraCheck(control);
            if (check) {
                setChecked(control,!isChecked(control));
                if(control==dominantHand)updateLeftHandLayout();
                InvalidateRect(control, nullptr, TRUE);
                autoSaveSettings();
                return 0;
            }
        }
        switch (LOWORD(wp)) {
        case IdLaunch: runLauncher(true); break;
        case IdDiagnose: runLauncher(false); break;
        case IdLogs:
            std::filesystem::create_directories(root / L"logs");
            ShellExecuteW(hwnd, L"open", (root / L"logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IdApplyCalibration:
            try{
                const bool pose=selected(calibrationMode)==1||selected(calibrationMode)==2;
                if(pose){
                    calibrationApplyCommand.revision=std::max(calibrationApplyCommand.revision+1,uint64_t(GetTickCount64()));
                    calibrationApplyCommand.mode=calibrationModes[selected(calibrationMode)];
                    calibrationApplyCommand.profile=calibrationProfiles.at(std::max(0,selected(calibrationProfile)));
                    calibrationApplyCommand.left=isChecked(dominantHand);calibrationApplyWaiting=true;
                }
                writeControlsConfig();setStatus(pose?L"Apply requested. Return to the running game to finish saving.":L"Calibration settings applied.");
            }catch(...){calibrationApplyWaiting=false;setStatus(L"Cannot save calibration settings.");}
            break;
        case IdCalibrationFiles:
            ShellExecuteW(hwnd,L"open",root.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            break;
        case IdBrowse: {
            BROWSEINFOW browse{};
            browse.hwndOwner=hwnd;
            browse.lpszTitle=L"Select the DOOM Eternal folder (the folder containing DOOMEternalx64vk.exe)";
            browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            if(auto selection=SHBrowseForFolderW(&browse)){
                wchar_t folder[MAX_PATH]{};
                if(SHGetPathFromIDListW(selection,folder)){
                    const auto chosen=std::filesystem::path(folder);
                    for(const auto& candidate:{chosen/L"DOOMEternalx64vk.exe",chosen/L"Content/DOOMEternalx64vk.exe",findMicrosoftStoreGame(chosen)}){
                        if(validGameExecutable(candidate)){SetWindowTextW(gameEdit,candidate.c_str());break;}
                    }
                }
                CoTaskMemFree(selection);
            }
            break;
        }
        }
        return 0;
    case WM_TIMER:
        if(calibrationApplyWaiting){
            std::ifstream status(root/L"calibration_apply_status.txt");uint64_t revision{};int success{};
            if(status>>revision>>success&&revision==calibrationApplyCommand.revision){
                calibrationApplyWaiting=false;
                std::string target;std::getline(status,target);
                const std::wstring message=success?L"Saved:"+std::wstring(target.begin(),target.end()):L"Pose not saved. Check the selected target/folder and retry APPLY.";
                setStatus(message.c_str());
            }
        }
        if (worker && WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
            DWORD code{};
            GetExitCodeProcess(worker, &code);
            CloseHandle(worker);
            worker = nullptr;
            EnableWindow(launchButton, TRUE);
            if (code == 0) setStatus(L"Ready");
            else {
                const auto error = lastLauncherError();
                setStatus(error.empty() ? L"Startup check failed. Details are available in Development." : error.c_str());
            }
        }
        return 0;
    case WM_QUERYENDSESSION:
        return autoSaveSettings()?TRUE:FALSE;
    case WM_CLOSE:
        if(introThreadId){introCancelled=true;PostThreadMessageW(introThreadId,WM_QUIT,0,0);setStatus(L"Closing VR intro; game launch cancelled.");return 0;}
        if(autoSaveSettings())DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        settingsReady=false;
        if (worker) CloseHandle(worker);
        delete controllerBindingImage;
        delete logo;
        if (uiFont) DeleteObject(uiFont);
        if (titleFont) DeleteObject(titleFont);
        if (backgroundBrush) DeleteObject(backgroundBrush);
        if (panelBrush) DeleteObject(panelBrush);
        if (fieldBrush) DeleteObject(fieldBrush);
        if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

