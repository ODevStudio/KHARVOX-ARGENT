#include <algorithm>
#include <cmath>
#include <chrono>
#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <regex>
#include <filesystem>
#include <fstream>
#include <string>
#include "LauncherSettings.h"
#include "openxr/ConfigurationFile.h"
#include "LauncherProbeOutput.h"
#include "MouseSession.h"
#include "BuildFeatures.h"
#include "RenderResolution.h"
#include "DesktopMirrorResolution.h"
#include "hands/HandWeaponProfile.h"
#include "hands/CalibrationDraft.h"
#include <vector>
int runArgentVrIntro(const wchar_t* loaderPath);

namespace {
using argent::cleanRelease;

bool runningUnderWine() {
    // Proton and other Wine-based compatibility layers expose this ntdll export.
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

// Query the active driver state, not the registry preference pending a reboot.
// Missing APIs or unsupported queries are unknown, never evidence of enabled HAGS.
bool hardwareGpuSchedulingEnabled() {
    const HMODULE gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!gdi) return false;
    const auto enumerate = reinterpret_cast<PFND3DKMT_ENUMADAPTERS2>(GetProcAddress(gdi, "D3DKMTEnumAdapters2"));
    const auto query = reinterpret_cast<PFND3DKMT_QUERYADAPTERINFO>(GetProcAddress(gdi, "D3DKMTQueryAdapterInfo"));
    const auto close = reinterpret_cast<PFND3DKMT_CLOSEADAPTER>(GetProcAddress(gdi, "D3DKMTCloseAdapter"));
    bool enabled = false;
    if (enumerate && query && close) {
        D3DKMT_ADAPTERINFO adapters[64]{};
        D3DKMT_ENUMADAPTERS2 enumeration{};
        enumeration.NumAdapters = 64;
        enumeration.pAdapters = adapters;
        if (enumerate(&enumeration) >= 0) {
            for (ULONG i = 0; i < std::min(enumeration.NumAdapters, ULONG(64)); ++i) {
                D3DKMT_WDDM_2_7_CAPS caps{};
                D3DKMT_QUERYADAPTERINFO info{};
                info.hAdapter = adapters[i].hAdapter;
                info.Type = KMTQAITYPE_WDDM_2_7_CAPS;
                info.pPrivateDriverData = &caps;
                info.PrivateDriverDataSize = sizeof(caps);
                if (query(&info) >= 0 && caps.HwSchEnabled) enabled = true;
                D3DKMT_CLOSEADAPTER closing{};
                closing.hAdapter = adapters[i].hAdapter;
                close(&closing);
            }
        }
    }
    FreeLibrary(gdi);
    return enabled;
}

void configureStereoDlss(std::map<std::wstring,std::wstring>& env,int mode){
    // DLSS can be toggled in-game. Every stereo runtime needs both histories,
    // including SteamVR; override stale inherited startup-workaround values.
    env[L"ARGENT_DLSS_STEREO"]=mode==1?L"1":L"0";
}

std::filesystem::path configureShaderCapture(std::map<std::wstring,std::wstring>& env,
    bool enabled,const std::filesystem::path& package,const std::wstring& session){
    // Explicitly mask an inherited capture directory when the checkbox is off.
    env[L"ARGENT_CAPTURE_DIRECTORY"]=L"";
    env[L"ARGENT_WATER_INPUT_CAPTURE"]=enabled?L"1":L"0";
    env[L"ARGENT_WATER_GPU_CAPTURE"]=enabled?L"1":L"0";
    env[L"ARGENT_DECAL_GPU_CAPTURE"]=L"0";
    if(!enabled)return {};
    const auto parent=package/L"captures";
    std::filesystem::create_directories(parent);
    const auto capture=parent/session;
    if(!std::filesystem::create_directory(capture))throw std::runtime_error("Capture session already exists");
    std::ofstream metadata(capture/L"capture-session.txt",std::ios::binary);
    metadata<<"Native launcher shader capture\nOriginal SPIR-V: spirv/\nPipeline identities: graphics.tsv and compute.tsv\n";
    metadata.close();
    if(!metadata)throw std::runtime_error("Cannot write capture metadata");
    if(std::filesystem::exists(package/L"build-info.json"))
        std::filesystem::copy_file(package/L"build-info.json",capture/L"build-info.json");
    env[L"ARGENT_CAPTURE_DIRECTORY"]=capture.wstring();
    return capture;
}

enum : int {
    IdLaunch = 100, IdDiagnose, IdLogs, IdBrowse, IdTabs, IdGamePath,
    IdRenderer, IdCaptureShaders, IdSimulator, IdTurnMode, IdSnapAngle,
    IdMovement, IdDominantHand, IdLeftHandSwap, IdShowIntro, IdTwoHand, IdDisableAa, IdRenderScale,
    IdFsr, IdSmoothSpeed, IdPhysicalKill, IdKillSpeed, IdKillHands, IdAutomatic, IdSync, IdTraversal, IdShoulder,
    IdHandSmoothing, IdHandsJump, IdVirtualGunstock, IdBhaptics, IdPsvr2Triggers,
    IdExtendedLogging, IdShowHands, IdCalibrationMode, IdCalibrationProfile, IdApplyCalibration, IdCalibrationFiles, IdPivotForward, IdPivotLeft, IdPivotUp,
    IdCinematics3d, IdGpuDiagnostics, IdDisableVrIntro, IdRenderingLogging, IdCredit, IdDesktopMirror, IdMirrorResolution, IdTabBase = 200
};

HWND tabButtons[7]{}, gameEdit{}, statusText{}, launchButton{};
HWND renderScale{}, disableAa{}, captureShaders{}, simulator{}, turnMode{}, snapAngle{}, movement{}, dominantHand{},leftHandSwap{},leftHandLayoutLabel{};
HWND showIntro{}, twoHand{}, virtualGunstock{}, bhaptics{}, psvr2Triggers{}, tabPages[7]{};
HWND fsr{},fsrStatus{},smoothSpeed{},physicalKill{},killSpeed{},killHands{};
HWND automatic{},syncImmersive{},traversal{},shoulder{};
HWND extendedLogging{},showHands{},handsJump{},handSmoothing{},calibrationMode{},calibrationProfile{},weaponPivot[3]{};
argent::calibration::ApplyCommand calibrationApplyCommand;bool calibrationApplyWaiting{};
HWND cinematics3d{},gpuDiagnostics{};
HWND disableVrIntro{}, renderingLogging{}, desktopMirror{}, mirrorResolution{};
bool advancedTabs{};
bool vrIntroStarted{},launchInProgress{},introCancelled{};
DWORD introThreadId{};
std::vector<std::string> calibrationProfiles(kharvox::hands::handProfileKeys.begin(),kharvox::hands::handProfileKeys.end());
const char* calibrationModes[]{"off","hands","weapon","support","hud"};
HANDLE worker{};
HFONT uiFont{}, titleFont{};
HBRUSH backgroundBrush{}, panelBrush{}, fieldBrush{};
Gdiplus::Image* logo{};
ULONG_PTR gdiplusToken{};
std::filesystem::path root;
int activeTab{};
std::map<HWND,bool> checks;

constexpr COLORREF Background = RGB(0, 0, 0);
constexpr COLORREF Panel = RGB(0, 0, 0);
constexpr COLORREF Field = RGB(0, 0, 0);
constexpr COLORREF Border = RGB(62, 62, 66);
constexpr COLORREF Accent = RGB(255, 61, 12);
constexpr COLORREF Text = RGB(245, 245, 245);
constexpr COLORREF Muted = RGB(155, 155, 160);

std::wstring quote(const std::wstring& text) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
        else { out.append(slashes, L'\\'); out += c; }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

int selected(HWND hwnd) { return static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0)); }
bool isChecked(HWND hwnd) { auto i=checks.find(hwnd);return i!=checks.end()&&i->second; }
void setChecked(HWND hwnd,bool value){
    checks[hwnd]=value;InvalidateRect(hwnd,nullptr,TRUE);
    if(hwnd&&(hwnd==extendedLogging||hwnd==renderingLogging)){
        HWND other=hwnd==extendedLogging?renderingLogging:extendedLogging;
        if(other){checks[other]=value;InvalidateRect(other,nullptr,TRUE);}
    }

}
void setStatus(const wchar_t* text) { SetWindowTextW(statusText, text); }
std::wstring lastLauncherError() {
    std::wifstream file(root / L"logs/launcher-output.txt");
    if (!file) return {};
    std::wstring line, last;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) last = line;
    }
    if (last.size() > 180) last = last.substr(0, 177) + L"...";
    return last;
}
std::wstring timestampName() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    wchar_t text[64]{};
    swprintf_s(text, L"%04u%02u%02u-%02u%02u%02u-%03u",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return text;
}
std::wstring processOutput(const std::filesystem::path& exe, const std::map<std::wstring,std::wstring>& env) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &sa, 0)) return L"PROBE_START_FAILED win32="+std::to_wstring(GetLastError())+L"\n";
    if(!SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)){
        const auto error=GetLastError();CloseHandle(read);CloseHandle(write);
        return L"PROBE_START_FAILED win32="+std::to_wstring(error)+L"\n";
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write;
    si.hStdError = write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = quote(exe.wstring());
    auto block = GetEnvironmentStringsW();
    std::wstring environment;
    if (block) {
        for (auto p = block; *p; p += wcslen(p) + 1) {
            std::wstring row(p);
            auto eq = row.find(L'=');
            auto key = eq == std::wstring::npos ? row : row.substr(0, eq);
            bool replaced = false;
            for (const auto& entry : env)
                if (_wcsicmp(key.c_str(), entry.first.c_str()) == 0) { replaced = true; break; }
            if (!replaced) { environment += row; environment.push_back(L'\0'); }
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& entry : env) {
        environment += entry.first + L"=" + entry.second;
        environment.push_back(L'\0');
    }
    environment.push_back(L'\0');
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment.data(), root.c_str(), &si, &pi);
    const auto startError=ok?ERROR_SUCCESS:GetLastError();
    CloseHandle(write);
    std::wstring output;
    if (ok) {
        output=argent::launcher::collectProbeOutput(pi.hProcess,read);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        output = L"PROBE_START_FAILED win32=" + std::to_wstring(startError) + L"\n";
    }
    CloseHandle(read);
    return output;
}
bool containsLine(const std::wstring& text, const wchar_t* prefix) {
    return text.find(prefix) != std::wstring::npos;
}
int maxEyeValue(const std::wstring& text, bool width) {
    std::wregex pattern(L"XR_EYE[01]_RECOMMENDED=(\\d+)x(\\d+)");
    int result = 0;
    for (std::wsregex_iterator i(text.begin(), text.end(), pattern), end; i != end; ++i)
        result = std::max(result, _wtoi((*i)[width ? 1 : 2].str().c_str()));
    return result;
}
std::wstring hexToken() {
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t text[40]{};
    StringFromGUID2(guid, text, 40);
    std::wstring token(text);
    token.erase(std::remove_if(token.begin(), token.end(), [](wchar_t c){return c==L'{'||c==L'}'||c==L'-';}), token.end());
    return token;
}
bool isolatedSettings{};
bool settingsReady{};
std::filesystem::path isolatedSettingsRoot;
std::filesystem::path launcherSettingsPath(){
    if(isolatedSettings)return isolatedSettingsRoot/L"local-settings/launcher.ini";
    wchar_t folder[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,folder)))
        throw std::runtime_error("Cannot locate local application data");
    return std::filesystem::path(folder)/L"KHARVOX ARGENT/launcher.ini";
}
bool validGameExecutable(const std::filesystem::path& path){
    std::error_code ec;
    return _wcsicmp(path.filename().c_str(),L"DOOMEternalx64vk.exe")==0&&std::filesystem::is_regular_file(path,ec);
}
bool microsoftStoreGame(const std::filesystem::path& game){
    std::ifstream config(game.parent_path()/L"MicrosoftGame.Config");
    if(!config)return false;
    const std::string text((std::istreambuf_iterator<char>(config)),{});
    return text.find("BethesdaSoftworks.DOOMEternal-PC")!=std::string::npos;
}
bool processElevated(HANDLE process, bool& known) {
    known = false;
    HANDLE token{};
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes{};
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
    CloseHandle(token);
    known = ok;
    return ok && elevation.TokenIsElevated != 0;
}
bool currentProcessElevated(bool& known) {
    return processElevated(GetCurrentProcess(), known);
}
bool runningSteamElevated(bool& known) {
    known = true;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { known = false; return false; }
    PROCESSENTRY32W entry{sizeof(entry)};
    bool elevated = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"steam.exe") != 0) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) { known = false; continue; }
        bool oneKnown{};
        elevated |= processElevated(process, oneKnown);
        known &= oneKnown;
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return elevated;
}
bool doomRunAsAdmin(const std::wstring& gameExe, std::wstring& location, bool& known) {
    known = true;
    constexpr const wchar_t* subkey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY key{};
        if (RegOpenKeyExW(hive, subkey, 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) continue;
        DWORD type{}, bytes{};
        LONG sizeResult = RegQueryValueExW(key, gameExe.c_str(), nullptr, &type, nullptr, &bytes);
        if (sizeResult == ERROR_ACCESS_DENIED) known = false;
        if (sizeResult == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
            if (RegQueryValueExW(key, gameExe.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) == ERROR_SUCCESS) {
                value.resize(wcslen(value.c_str()));
                auto upper = value;
                std::transform(upper.begin(), upper.end(), upper.begin(), towupper);
                if (upper.find(L"RUNASADMIN") != std::wstring::npos) {
                    location = (hive == HKEY_CURRENT_USER ? L"HKCU " : L"HKLM ") + std::wstring(view == KEY_WOW64_64KEY ? L"Registry64\\" : L"Registry32\\") + subkey;
                    RegCloseKey(key);
                    return true;
                }
            }
        }
        RegCloseKey(key);
    }
    return false;
}
std::filesystem::path findMicrosoftStoreGame(const std::filesystem::path& xboxGames){
    std::error_code ec;
    for(std::filesystem::directory_iterator it(xboxGames,ec),end;!ec&&it!=end;it.increment(ec)){
        if(!it->is_directory(ec))continue;
        const auto game=it->path()/L"Content/DOOMEternalx64vk.exe";
        if(validGameExecutable(game)&&microsoftStoreGame(game))return game;
    }
    return {};
}
std::wstring findGameExecutable() {
    std::vector<std::filesystem::path> libraries;
    for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        HKEY key{};
        if(RegOpenKeyExW(hive,L"SOFTWARE\\Valve\\Steam",0,KEY_READ|view,&key)!=ERROR_SUCCESS)continue;
        for(auto name:{L"SteamPath",L"InstallPath",L"SteamExe"}){
            wchar_t text[32768]{};DWORD size=sizeof(text),type{};
            if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text),&size)==ERROR_SUCCESS&&type==REG_SZ&&text[0]){
                std::filesystem::path path(text);
                libraries.push_back(std::wstring(name)==L"SteamExe"?path.parent_path():path);
            }
        }
        RegCloseKey(key);
    }
    for(auto id:{CSIDL_PROGRAM_FILESX86,CSIDL_PROGRAM_FILES}){
        wchar_t folder[MAX_PATH]{};
        if(SUCCEEDED(SHGetFolderPathW(nullptr,id,nullptr,SHGFP_TYPE_CURRENT,folder)))libraries.push_back(std::filesystem::path(folder)/L"Steam");
    }
    auto roots=libraries;
    const std::regex pathEntry(R"vdf("path"\s+"([^"]+)")vdf");
    for(const auto& steam:roots){
        std::ifstream file(steam/L"steamapps/libraryfolders.vdf");
        std::string contents((std::istreambuf_iterator<char>(file)),{});
        for(std::sregex_iterator i(contents.begin(),contents.end(),pathEntry),end;i!=end;++i){
            auto value=(*i)[1].str();
            for(size_t at=0;(at=value.find("\\\\",at))!=std::string::npos;++at)value.replace(at,2,"\\");
            libraries.push_back(std::filesystem::u8path(value));
        }
    }
    std::vector<std::filesystem::path> candidates{root/L"DOOMEternalx64vk.exe",root.parent_path()/L"DOOMEternalx64vk.exe"};
    for(const auto& library:libraries)candidates.push_back(library/L"steamapps/common/DOOMEternal/DOOMEternalx64vk.exe");
    wchar_t drives[512]{};
    const DWORD driveChars=GetLogicalDriveStringsW(DWORD(std::size(drives)),drives);
    if(driveChars&&driveChars<std::size(drives))for(const wchar_t* drive=drives;*drive;drive+=wcslen(drive)+1){
        const auto xbox=findMicrosoftStoreGame(std::filesystem::path(drive)/L"XboxGames");
        if(!xbox.empty())candidates.push_back(xbox);
    }
    for(const auto& candidate:candidates)if(validGameExecutable(candidate))return candidate.wstring();
    return {};
}
std::wstring activeOpenXRManifestW() {
    DWORD size = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0);
    if (size > 1) {
        std::wstring value(size, L'\0');
        DWORD copied = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", value.data(), size);
        if (copied > 0 && copied < size) { value.resize(copied); return value; }
    }
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0,
        KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    DWORD type{}, bytes{};
    if (RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ)) { RegCloseKey(key); return {}; }
    std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
    LONG result = RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return {};
    value.resize(wcslen(value.c_str()));
    if (type == REG_EXPAND_SZ) {
        DWORD expandedSize = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (expandedSize > 1) {
            std::wstring expanded(expandedSize, L'\0');
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), expandedSize)) {
                expanded.resize(expandedSize - 1);
                value = std::move(expanded);
            }
        }
    }
    return value;
}
bool usesSteamVrRuntime() {
    auto manifest = activeOpenXRManifestW();
    std::transform(manifest.begin(), manifest.end(), manifest.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return manifest.find(L"steamvr") != std::wstring::npos
        || manifest.find(L"steamxr") != std::wstring::npos;
}
void updateRenderingAvailability(bool steamVr) {
    EnableWindow(renderScale,!steamVr);
    EnableWindow(fsr,!steamVr);
    SetWindowTextW(fsrStatus,steamVr
        ? L"SteamVR detected , adjust render scale within SteamVR."
        : L"FSR is disabled at 100% res or higher. Turn off for DLSS usage.");
    InvalidateRect(renderScale,nullptr,TRUE);
    InvalidateRect(fsr,nullptr,TRUE);
}
std::filesystem::path steamVrRootFromManifest(const std::wstring& manifest) {
    if (manifest.empty()) return {};
    auto path = std::filesystem::path(manifest).parent_path();
    if (_wcsicmp(path.filename().c_str(), L"bin") == 0) path = path.parent_path();
    return path;
}
void addSteamVrPathEnvironment(std::map<std::wstring,std::wstring>& env, const std::wstring& manifest) {
    auto steamVrRoot = steamVrRootFromManifest(manifest);
    if (!steamVrRoot.empty()) env[L"VR_OVERRIDE"] = steamVrRoot.wstring();
    wchar_t local[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) {
        auto pathRegistry = std::filesystem::path(local) / L"openvr/openvrpaths.vrpath";
        if (std::filesystem::exists(pathRegistry)) env[L"VR_PATHREG_OVERRIDE"] = pathRegistry.wstring();
    }
}
std::wstring findSteamExecutable() {
    for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        HKEY key{};
        if(RegOpenKeyExW(hive,L"SOFTWARE\\Valve\\Steam",0,KEY_READ|view,&key)!=ERROR_SUCCESS)continue;
        for(auto name:{L"SteamExe",L"SteamPath",L"InstallPath"}){
            wchar_t text[32768]{};DWORD size=sizeof(text),type{};
            if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text),&size)==ERROR_SUCCESS&&type==REG_SZ&&text[0]){
                RegCloseKey(key);
                return std::wstring(name)==L"SteamExe" ? std::filesystem::path(text).wstring()
                    : (std::filesystem::path(text)/L"steam.exe").wstring();
            }
        }
        RegCloseKey(key);
    }
    return {};
}
void ensureSteamVrArgentSettings(std::wofstream& log) {
    auto steam = findSteamExecutable();
    if (steam.empty()) return;
    auto settings = std::filesystem::path(steam).parent_path() / L"config/steamvr.vrsettings";
    std::error_code ec;
    std::filesystem::create_directories(settings.parent_path(), ec);
    std::ifstream input(settings, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), {});
    auto updated = text;
    if (updated.empty()) updated = "{\r\n   \"steam.app.782330\" : {\r\n      \"disableAsync\" : false\r\n   }\r\n}\r\n";
    else if (updated.find("\"steam.app.782330\"") == std::string::npos) {
        auto close = updated.find_last_of('}');
        if (close != std::string::npos)
            updated.insert(close, std::string(updated.find(':') == std::string::npos ? "\r\n" : ",\r\n") +
                "   \"steam.app.782330\" : {\r\n      \"disableAsync\" : false\r\n   }\r\n");
    } else {
        std::regex blockPattern(R"(("steam\.app\.782330"\s*:\s*\{[^}]*))", std::regex::icase);
        std::smatch match;
        if (std::regex_search(updated, match, blockPattern)) {
            std::string block = match.str(1);
            std::regex disablePattern(R"(("disableAsync"\s*:\s*)(true|false))", std::regex::icase);
            std::string replacement = std::regex_search(block, disablePattern)
                ? std::regex_replace(block, disablePattern, "$1false")
                : block + ",\r\n      \"disableAsync\" : false";
            updated.replace(static_cast<size_t>(match.position(1)), static_cast<size_t>(match.length(1)), replacement);
        }
    }
    if (updated != text) {
        if (!text.empty() && !std::filesystem::exists(settings.wstring() + L".argent-backup", ec))
            std::filesystem::copy_file(settings, settings.wstring() + L".argent-backup", ec);
        std::ofstream output(settings, std::ios::binary | std::ios::trunc);
        output << updated;
        if (log) log << L"SteamVR settings: disableAsync=false for steam.app.782330\n";
    } else if (log) log << L"SteamVR settings: ARGENT already configured\n";
}

HWND addControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
    int x, int y, int w, int h, int id) {
    HWND c = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    return c;
}

HWND addLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, bool muted = false) {
    HWND label = addControl(parent, L"STATIC", text, 0, x, y, w, h, 0);
    SetWindowLongPtrW(label, GWLP_USERDATA, muted ? 1 : 0);
    return label;
}

HWND addCheck(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, bool on = false) {
    HWND c = addControl(parent, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
    setChecked(c,on);
    return c;
}

HWND addCombo(HWND parent, int x, int y, int w, int h, int id,
    std::initializer_list<const wchar_t*> items, int index) {
    HWND c = addControl(parent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED | CBS_DISABLENOSCROLL | WS_VSCROLL | WS_TABSTOP, x, y, w, h, id);
    for (auto item : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(c, CB_SETCURSEL, index, 0);
    return c;
}

bool extraCheck(HWND h){return h==handSmoothing||h==handsJump||h==dominantHand||h==desktopMirror||h==renderingLogging||h==disableVrIntro||h==cinematics3d||h==fsr||h==physicalKill||h==virtualGunstock||h==bhaptics||h==psvr2Triggers||h==extendedLogging||h==gpuDiagnostics||h==showHands;}
void configureGpuDiagnostics(std::map<std::wstring,std::wstring>& env,bool enabled){env[L"ARGENT_GPU_DIAGNOSTICS"]=enabled?L"1":L"0";}
std::wstring loggingArgument(){return isChecked(extendedLogging)?L" -ExtendedLogging":L"";}
#include "LauncherStartup.inc"
#include "LauncherHelp.inc"
#include "LauncherWindow.inc"
}

#include "LauncherMain.inc"
