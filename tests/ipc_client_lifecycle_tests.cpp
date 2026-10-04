#include <Windows.h>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fixture {
std::atomic<bool> pauseStop{}, pauseRetirement{}, pauseConnection{}, signalInUse{}, unsafeClose{};
std::atomic<bool> failEvent{}, failThread{};
std::atomic<bool> failConfiguration{};
std::atomic<HANDLE> firstStop{};
std::atomic<unsigned> eventsCreated{};
HANDLE entered{}, resume{}, retiring{};
std::vector<HANDLE> workers;
std::mutex workersMutex;
HANDLE retainedEvent{};
HANDLE WINAPI createEvent(SECURITY_ATTRIBUTES*, BOOL, BOOL, LPCWSTR);
HANDLE WINAPI createThread(SECURITY_ATTRIBUTES*, SIZE_T, LPTHREAD_START_ROUTINE, void*, DWORD, DWORD*);
BOOL WINAPI close(HANDLE);
BOOL WINAPI signal(HANDLE);
BOOL WINAPI waitPipe(LPCWSTR, DWORD);
DWORD WINAPI waitOne(HANDLE, DWORD);
DWORD WINAPI environment(LPCWSTR, LPWSTR, DWORD);
}

#define CreateEventW fixture::createEvent
#define CreateThread fixture::createThread
#define CloseHandle fixture::close
#define SetEvent fixture::signal
#define WaitNamedPipeW fixture::waitPipe
#define WaitForSingleObject fixture::waitOne
#define GetEnvironmentVariableW fixture::environment
#if defined(ARGENT_IPC_PSVR2)
#include "../src/psvr2/Psvr2IpcClient.cpp"
#else
#include "../src/bhaptics/BhapticsIpcClient.cpp"
#endif
#undef CreateEventW
#undef CreateThread
#undef CloseHandle
#undef SetEvent
#undef WaitNamedPipeW
#undef WaitForSingleObject
#undef GetEnvironmentVariableW

namespace fixture {
HANDLE WINAPI createEvent(SECURITY_ATTRIBUTES* security, BOOL manual, BOOL signaled, LPCWSTR name) {
    if (failEvent.exchange(false)) return nullptr;
    const HANDLE event = CreateEventW(security, manual, signaled, name);
    if (event && eventsCreated.fetch_add(1) == 0) firstStop.store(event);
    return event;
}
HANDLE WINAPI createThread(SECURITY_ATTRIBUTES* security, SIZE_T stack, LPTHREAD_START_ROUTINE entry,
    void* context, DWORD flags, DWORD* id) {
    if (failThread.exchange(false)) return nullptr;
    const HANDLE worker = CreateThread(security, stack, entry, context, flags, id);
    if (worker) {
        HANDLE observer{};
        if (!DuplicateHandle(GetCurrentProcess(), worker, GetCurrentProcess(), &observer,
                0, FALSE, DUPLICATE_SAME_ACCESS)) std::abort();
        const std::lock_guard<std::mutex> lock(workersMutex);
        workers.push_back(observer);
    }
    return worker;
}
BOOL WINAPI close(HANDLE handle) {
    if (handle == firstStop.load()) {
        SetEvent(retiring);
        if (pauseRetirement.load()) {
            SetEvent(entered);
            WaitForSingleObject(resume, 5000);
        }
        if (signalInUse.load()) {
            unsafeClose.store(true);
            retainedEvent = handle;
            return TRUE;
        }
    }
    return CloseHandle(handle);
}
BOOL WINAPI signal(HANDLE handle) {
    if (handle == firstStop.load() && pauseStop.load()) {
        signalInUse.store(true);
        SetEvent(entered);
        WaitForSingleObject(resume, 5000);
        const BOOL result = SetEvent(handle);
        signalInUse.store(false);
        return result;
    }
    return SetEvent(handle);
}
BOOL WINAPI waitPipe(LPCWSTR, DWORD) {
    if (pauseConnection.load()) {
        SetEvent(entered);
        WaitForSingleObject(resume, 5000);
    }
    Sleep(1);
    SetLastError(ERROR_FILE_NOT_FOUND);
    return FALSE;
}
DWORD WINAPI waitOne(HANDLE handle, DWORD timeout) {
    if (timeout == 250) return WaitForSingleObject(handle, 1);
    return WaitForSingleObject(handle, timeout);
}
DWORD WINAPI environment(LPCWSTR name, LPWSTR buffer, DWORD size) {
    if (failConfiguration.exchange(false)) throw std::bad_alloc{};
    return GetEnvironmentVariableW(name, buffer, size);
}
}

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
static void startClient() {
#if defined(ARGENT_IPC_PSVR2)
    KharvoxPsvr2IpcStart();
#else
    KharvoxBhapticsIpcStart();
#endif
}
static void stopClient() {
#if defined(ARGENT_IPC_PSVR2)
    KharvoxPsvr2IpcRequestStop();
#else
    KharvoxBhapticsIpcRequestStop();
#endif
}
static void joinWorkers() {
    for (const auto worker : fixture::workers) {
        check(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, "IPC worker did not exit");
        DWORD result{};
        check(GetExitCodeThread(worker, &result) && result == 0, "IPC worker failed");
        CloseHandle(worker);
    }
    if (fixture::retainedEvent) CloseHandle(fixture::retainedEvent);
    check(!workerStarted.load() && !stopEvent.load() && !workerHandle,
        "IPC worker ownership was not cleared");
}

static void stopRace() {
    fixture::pauseStop.store(true);
    startClient();
    std::thread stopping(stopClient);
    const bool entered = WaitForSingleObject(fixture::entered, 2000) == WAIT_OBJECT_0;
    const bool closedEarly = WaitForSingleObject(fixture::retiring, 100) == WAIT_OBJECT_0;
    SetEvent(fixture::resume);
    stopping.join();
    joinWorkers();
    check(entered, "Stop signal hook was not reached");
    check(!closedEarly && !fixture::unsafeClose.load(), "IPC event closed while a caller was signaling it");
}

static void restartRace(bool beforeRetirement) {
    fixture::pauseConnection.store(beforeRetirement);
    fixture::pauseRetirement.store(!beforeRetirement);
    startClient();
    if (beforeRetirement) {
        check(WaitForSingleObject(fixture::entered, 2000) == WAIT_OBJECT_0,
            "IPC worker did not enter the connection hook");
    }
    stopClient();
    if (!beforeRetirement) {
        check(WaitForSingleObject(fixture::entered, 2000) == WAIT_OBJECT_0,
            "IPC worker did not enter the retirement hook");
    }
    const HANDLE restarted = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(restarted != nullptr, "Cannot create restart observer");
    std::thread starting([restarted] { startClient(); SetEvent(restarted); });
    const bool returnedEarly = WaitForSingleObject(restarted, 100) == WAIT_OBJECT_0;
    fixture::pauseConnection.store(false);
    fixture::pauseRetirement.store(false);
    SetEvent(fixture::resume);
    starting.join();
    stopClient();
    joinWorkers();
    CloseHandle(restarted);
    check(!returnedEarly, "IPC start returned before the previous worker retired");
    check(fixture::eventsCreated.load() == 2, "A quick IPC restart was lost");
}

static void startFailure(bool eventFailure) {
    fixture::failEvent.store(eventFailure);
    fixture::failThread.store(!eventFailure);
    startClient();
    check(!workerStarted.load() && !stopEvent.load(), "Failed startup retained IPC ownership");
    startClient();
    stopClient();
    joinWorkers();
    check(fixture::workers.size() == 1, "IPC startup did not recover after failure");
}

static void configurationFailure() {
    fixture::failConfiguration.store(true);
    bool threw{};
    try { startClient(); } catch (const std::bad_alloc&) { threw = true; }
    check(threw && !workerStarted.load() && !stopEvent.load(),
        "Configuration exception claimed worker ownership without starting a thread");
    startClient();
    stopClient();
    joinWorkers();
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Expected a lifecycle scenario");
        fixture::entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        fixture::resume = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        fixture::retiring = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(fixture::entered && fixture::resume && fixture::retiring, "Cannot create lifecycle observers");
#if defined(ARGENT_IPC_PSVR2)
        check(SetEnvironmentVariableW(L"KHARVOX_USE_PSVR2_TOOLKIT", L"1")
            && SetEnvironmentVariableW(L"KHARVOX_PSVR2_PIPE_NAME", L"test-lifecycle")
            && SetEnvironmentVariableW(L"KHARVOX_PSVR2_SESSION_TOKEN", L"0123456789abcdef"),
            "Cannot configure PSVR2 test client");
#else
        check(SetEnvironmentVariableW(L"KHARVOX_BHAPTICS_PIPE_NAME", L"test-lifecycle")
            && SetEnvironmentVariableW(L"KHARVOX_BHAPTICS_SESSION_TOKEN", L"0123456789abcdef"),
            "Cannot configure bHaptics test client");
#endif
        const std::string mode = argv[1];
        if (mode == "stop-race") stopRace();
        else if (mode == "restart-race") restartRace(false);
        else if (mode == "rapid-restart") restartRace(true);
        else if (mode == "event-failed") startFailure(true);
        else if (mode == "thread-failed") startFailure(false);
        else if (mode == "configuration-error") configurationFailure();
        else throw std::runtime_error("Unknown lifecycle scenario");
        CloseHandle(fixture::entered);
        CloseHandle(fixture::resume);
        CloseHandle(fixture::retiring);
        std::cout << mode << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
