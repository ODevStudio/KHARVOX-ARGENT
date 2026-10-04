#pragma once
#include <Windows.h>
#include <cstdlib>

namespace kharvox {
inline void cancelAndDrainOverlappedIo(HANDLE handle, OVERLAPPED& operation) noexcept {
    CancelIoEx(handle, &operation);
    if (WaitForSingleObject(operation.hEvent, INFINITE) != WAIT_OBJECT_0) {
        RaiseFailFastException(nullptr, nullptr, 0);
        std::abort();
    }
}
}
