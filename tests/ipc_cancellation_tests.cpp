#include <Windows.h>
#include <Aclapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "../src/bhaptics/BhapticsSdkBackend.h"
#include "../src/psvr2/Psvr2ToolkitBackend.h"

namespace fixture {
enum class Scenario { Normal, PendingSuccess, BrokenPipe, Timeout, Stop, WaitFailure, CancelNotFound };
Scenario scenario{};
const HANDLE pipe = reinterpret_cast<HANDLE>(std::uintptr_t{1});
const HANDLE parent = reinterpret_cast<HANDLE>(std::uintptr_t{2});
HANDLE operationEvent{};
DWORD transferBytes{};
unsigned parentChecks{}, cancellations{};
std::atomic<bool> pending{};
bool unsafeClose{}, retainedEvent{};
std::thread completion;

BOOL begin(DWORD bytes, DWORD* transferred, OVERLAPPED* operation) {
    operationEvent = operation->hEvent;
    transferBytes = bytes;
    if (scenario == Scenario::Normal) {
        if (transferred) *transferred = bytes;
        return TRUE;
    }
    if (scenario == Scenario::BrokenPipe) {
        SetLastError(ERROR_BROKEN_PIPE);
        return FALSE;
    }
    pending.store(scenario != Scenario::PendingSuccess);
    if (!pending.load()) SetEvent(operationEvent);
    SetLastError(ERROR_IO_PENDING);
    return FALSE;
}

BOOL WINAPI write(HANDLE, const void*, DWORD bytes, DWORD* transferred, OVERLAPPED* operation) {
    return begin(bytes, transferred, operation);
}
BOOL WINAPI read(HANDLE, void*, DWORD bytes, DWORD* transferred, OVERLAPPED* operation) {
    return begin(bytes, transferred, operation);
}
BOOL WINAPI connect(HANDLE, OVERLAPPED* operation) {
    return begin(0, nullptr, operation);
}
HANDLE WINAPI createPipe(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, SECURITY_ATTRIBUTES*) {
    return pipe;
}
DWORD WINAPI waitOne(HANDLE handle, DWORD timeout) {
    if (handle == parent) {
        const bool alive = parentChecks++ == 0 || scenario == Scenario::Normal
            || scenario == Scenario::PendingSuccess || scenario == Scenario::BrokenPipe;
        return alive ? WAIT_TIMEOUT : WAIT_OBJECT_0;
    }
    return WaitForSingleObject(handle, timeout);
}
DWORD WINAPI waitMany(DWORD, const HANDLE*, BOOL, DWORD) {
    if (scenario == Scenario::Normal || scenario == Scenario::PendingSuccess) return WAIT_OBJECT_0;
    if (scenario == Scenario::Stop) return WAIT_OBJECT_0 + 1;
    if (scenario == Scenario::WaitFailure) {
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    return WAIT_TIMEOUT;
}
BOOL WINAPI cancel(HANDLE handle, OVERLAPPED* operation) {
    if (handle != pipe) return CancelIoEx(handle, operation);
    ++cancellations;
    const HANDLE event = operationEvent;
    completion = std::thread([event] {
        Sleep(120);
        pending.store(false);
        SetEvent(event);
    });
    if (scenario == Scenario::CancelNotFound) {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}
BOOL WINAPI result(HANDLE, OVERLAPPED*, DWORD* transferred, BOOL) {
    if (pending.load()) {
        SetLastError(ERROR_IO_INCOMPLETE);
        return FALSE;
    }
    *transferred = transferBytes;
    return TRUE;
}
BOOL WINAPI close(HANDLE handle) {
    if (pending.load() && (handle == operationEvent || handle == pipe)) {
        unsafeClose = true;
        if (handle == operationEvent) retainedEvent = true;
        return TRUE;
    }
    return handle == pipe ? TRUE : CloseHandle(handle);
}
}

#define WriteFile fixture::write
#define ReadFile fixture::read
#define ConnectNamedPipe fixture::connect
#define CreateNamedPipeW fixture::createPipe
#define WaitForSingleObject fixture::waitOne
#define WaitForMultipleObjects fixture::waitMany
#define CancelIoEx fixture::cancel
#define GetOverlappedResult fixture::result
#define CloseHandle fixture::close
#define wmain unusedBridgeEntry
#if defined(ARGENT_IPC_CLIENT)
#if defined(ARGENT_IPC_PSVR2)
#include "../src/psvr2/Psvr2IpcClient.cpp"
#else
#include "../src/bhaptics/BhapticsIpcClient.cpp"
#endif
#else
#if defined(ARGENT_IPC_PSVR2)
#include "../src/psvr2/Psvr2BridgeMain.cpp"
#else
#include "../src/bhaptics/BhapticsBridgeMain.cpp"
#endif
#endif
#undef wmain
#undef WriteFile
#undef ReadFile
#undef ConnectNamedPipe
#undef CreateNamedPipeW
#undef WaitForSingleObject
#undef WaitForMultipleObjects
#undef CancelIoEx
#undef GetOverlappedResult
#undef CloseHandle

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void nativeCancellation() {
    const auto path = L"\\\\.\\pipe\\kharvox-cancellation-test-" + std::to_wstring(GetCurrentProcessId());
    const HANDLE pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 4096, 4096, 1000, nullptr);
    check(pipe != INVALID_HANDLE_VALUE, "Cannot create native test pipe");
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    check(event != nullptr, "Cannot create native completion event");
    OVERLAPPED operation{};
    operation.hEvent = event;
    check(!ConnectNamedPipe(pipe, &operation) && GetLastError() == ERROR_IO_PENDING,
        "Native connect did not pend");
    kharvox::cancelAndDrainOverlappedIo(pipe, operation);
    DWORD bytes{};
    check(!GetOverlappedResult(pipe, &operation, &bytes, FALSE)
        && GetLastError() == ERROR_OPERATION_ABORTED, "Native connect cancellation was not completed");
    operation = {};
    operation.hEvent = event;
    ResetEvent(event);
    check(!ConnectNamedPipe(pipe, &operation) && GetLastError() == ERROR_IO_PENDING,
        "Native reconnect did not pend");
    const HANDLE client = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    check(client != INVALID_HANDLE_VALUE, "Cannot connect native test client");
    check(WaitForSingleObject(event, 2000) == WAIT_OBJECT_0, "Native connect did not complete");
    kharvox::cancelAndDrainOverlappedIo(pipe, operation);
    check(GetOverlappedResult(pipe, &operation, &bytes, FALSE) != FALSE,
        "Completion racing cancellation was rejected");
    operation = {};
    operation.hEvent = event;
    ResetEvent(event);
    std::array<char, 16> buffer{};
    check(!ReadFile(pipe, buffer.data(), DWORD(buffer.size()), &bytes, &operation)
        && GetLastError() == ERROR_IO_PENDING, "Native read did not pend");
    kharvox::cancelAndDrainOverlappedIo(pipe, operation);
    check(!GetOverlappedResult(pipe, &operation, &bytes, FALSE)
        && GetLastError() == ERROR_OPERATION_ABORTED, "Native read cancellation was not completed");
    CloseHandle(client);
    CloseHandle(event);
    CloseHandle(pipe);
    std::cout << "native connect/read cancellation and already-completed operation passed\n";
}

static void scenario(fixture::Scenario mode, bool connection) {
    fixture::scenario = mode;
    fixture::parentChecks = fixture::cancellations = 0;
    fixture::unsafeClose = fixture::retainedEvent = false;
    fixture::pending.store(false);
    fixture::operationEvent = nullptr;
#if defined(ARGENT_IPC_PSVR2) && !defined(ARGENT_IPC_CLIENT)
    shutdownRequested.store(false);
#endif
    std::array<char, 16> buffer{};
    const auto started = GetTickCount64();
#if defined(ARGENT_IPC_CLIENT)
    const bool success = writeWithTimeout(fixture::pipe, buffer.data(), DWORD(buffer.size()));
#else
    const bool success = connection
        ? createAndConnectPipe(L"unused", nullptr, fixture::parent) == fixture::pipe
        : readExact(fixture::pipe, fixture::parent, buffer.data(), DWORD(buffer.size()), 1);
#endif
    const bool returnedPending = fixture::pending.load();
    const auto elapsed = GetTickCount64() - started;
    if (fixture::completion.joinable()) fixture::completion.join();
    if (fixture::retainedEvent) CloseHandle(fixture::operationEvent);
    check(!returnedPending && !fixture::unsafeClose,
        "Production IPC path returned or closed storage before cancellation completed");
    const bool expectedSuccess = mode == fixture::Scenario::Normal
        || mode == fixture::Scenario::PendingSuccess;
    check(success == expectedSuccess, "Incorrect IPC success result");
    check(fixture::cancellations == (expectedSuccess || mode == fixture::Scenario::BrokenPipe ? 0u : 1u),
        "Unexpected cancellation count");
    if (fixture::cancellations) check(elapsed >= 100, "Cancellation was not drained");
    std::cout << (connection ? "connect" : "transfer") << " scenario=" << int(mode)
        << " elapsedMs=" << elapsed << '\n';
}

int main() {
    try {
        nativeCancellation();
#if defined(ARGENT_IPC_CLIENT)
        const HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(stop != nullptr, "Cannot create stop event");
        stopEvent.store(stop);
#elif defined(ARGENT_IPC_PSVR2)
        shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        check(shutdownEvent != nullptr, "Cannot create shutdown event");
#endif
        for (const auto mode : {fixture::Scenario::Normal, fixture::Scenario::PendingSuccess,
                fixture::Scenario::BrokenPipe, fixture::Scenario::Timeout, fixture::Scenario::Stop,
                fixture::Scenario::WaitFailure, fixture::Scenario::CancelNotFound}) {
            scenario(mode, false);
#if !defined(ARGENT_IPC_CLIENT)
            scenario(mode, true);
#endif
        }
#if defined(ARGENT_IPC_CLIENT)
        CloseHandle(stopEvent.exchange(nullptr));
#elif defined(ARGENT_IPC_PSVR2)
        CloseHandle(shutdownEvent);
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
