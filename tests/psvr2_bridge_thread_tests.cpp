#include <Windows.h>
#include <Aclapi.h>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "../src/psvr2/Psvr2ToolkitBackend.h"

namespace fixture {
bool injectException{}, failStart{}, unsafeClose{};
HANDLE worker{}, stop{};
unsigned threadCloses{};
std::vector<HANDLE> retainedHandles;
std::filesystem::path missingExecutable;
HANDLE WINAPI startThread(SECURITY_ATTRIBUTES*, SIZE_T, LPTHREAD_START_ROUTINE, void*, DWORD, DWORD*);
BOOL WINAPI close(HANDLE);
ULONGLONG WINAPI tick();
DWORD WINAPI modulePath(HMODULE, wchar_t*, DWORD);
}

#define CreateThread fixture::startThread
#define CloseHandle fixture::close
#define GetTickCount64 fixture::tick
#define GetModuleFileNameW fixture::modulePath
#define wmain bridgeMain
#include "../src/psvr2/Psvr2BridgeMain.cpp"
#undef wmain
#undef CreateThread
#undef CloseHandle
#undef GetTickCount64
#undef GetModuleFileNameW

namespace fixture {
DWORD WINAPI delayedWorker(void*) {
    if (WaitForSingleObject(stop, 5000) != WAIT_OBJECT_0) return 2;
    Sleep(2200);
    return 0;
}
HANDLE WINAPI startThread(SECURITY_ATTRIBUTES*, SIZE_T, LPTHREAD_START_ROUTINE, void*, DWORD, DWORD*) {
    if (failStart) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    stop = shutdownEvent;
    worker = CreateThread(nullptr, 0, delayedWorker, nullptr, 0, nullptr);
    return worker;
}
BOOL WINAPI close(HANDLE handle) {
    const bool running = worker && WaitForSingleObject(worker, 0) != WAIT_OBJECT_0;
    if (handle == worker) {
        ++threadCloses;
        unsafeClose |= running;
        return TRUE;
    }
    if (running && (handle == stop || GetFileType(handle) == FILE_TYPE_UNKNOWN)) {
        unsafeClose = true;
        retainedHandles.push_back(handle);
        return TRUE;
    }
    return CloseHandle(handle);
}
ULONGLONG WINAPI tick() {
    if (worker) {
        if (injectException) throw std::runtime_error("Injected bridge loop failure");
        shutdownRequested.store(true);
        SetEvent(stop);
    }
    return GetTickCount64();
}
DWORD WINAPI modulePath(HMODULE, wchar_t* destination, DWORD capacity) {
    const auto value = missingExecutable.wstring();
    if (value.size() >= capacity) return capacity;
    std::memcpy(destination, value.c_str(), (value.size() + 1) * sizeof(wchar_t));
    return DWORD(value.size());
}
}

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected normal, exception or start-failed scenario");
        fixture::injectException = !std::strcmp(argv[1], "exception");
        fixture::failStart = !std::strcmp(argv[1], "start-failed");
        check(fixture::injectException || fixture::failStart || !std::strcmp(argv[1], "normal"),
            "Unknown worker scenario");
        wchar_t executable[32768]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
        check(length && length < std::size(executable), "Cannot locate test executable");
        fixture::missingExecutable = std::filesystem::path(executable).parent_path()
            / ("missing-psvr2-backend-" + std::to_string(GetCurrentProcessId())) / "bridge.exe";
        check(!std::filesystem::exists(fixture::missingExecutable.parent_path()),
            "Backend test directory must not exist");
        const auto parent = std::to_wstring(GetCurrentProcessId());
        check(SetEnvironmentVariableW(L"KHARVOX_PSVR2_PIPE_NAME", L"test-worker")
            && SetEnvironmentVariableW(L"KHARVOX_PSVR2_SESSION_TOKEN", L"0123456789abcdef")
            && SetEnvironmentVariableW(L"KHARVOX_PSVR2_PARENT_PID", parent.c_str())
            && SetEnvironmentVariableW(L"KHARVOX_EXTENDED_LOGGING", L"0"), "Cannot set bridge test environment");
        const auto started = GetTickCount64();
        const int result = bridgeMain();
        const bool returnedRunning = fixture::worker
            && WaitForSingleObject(fixture::worker, 0) != WAIT_OBJECT_0;
        const auto elapsed = GetTickCount64() - started;
        if (fixture::worker) {
            if (returnedRunning) SetEvent(fixture::stop);
            check(WaitForSingleObject(fixture::worker, 5000) == WAIT_OBJECT_0,
                "Test worker did not exit");
            DWORD exit{};
            check(GetExitCodeThread(fixture::worker, &exit) && exit == 0, "Test worker failed");
            CloseHandle(fixture::worker);
        }
        for (const auto handle : fixture::retainedHandles) CloseHandle(handle);
        if (fixture::injectException && fixture::stop) CloseHandle(fixture::stop);
        check(!returnedRunning && !fixture::unsafeClose,
            "Bridge returned or released worker storage before the thread exited");
        check(result == (fixture::failStart ? 6 : fixture::injectException ? 1 : 0),
            "Incorrect bridge result");
        check(fixture::threadCloses == (fixture::failStart ? 0u : 1u),
            "Worker handle was not closed exactly once");
        if (!fixture::failStart) check(elapsed >= 2100, "Bridge did not join the delayed worker");
        std::cout << argv[1] << " elapsedMs=" << elapsed << " result=" << result << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
