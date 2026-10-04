// SourceRing::ownsImage() is called for every image barrier on every recording
// thread, so it reads a dedicated registry instead of the ring's main mutex
// (which acquire()/present() hold across fence waits and queue submits). These
// tests pin the registry's behavior against a fake driver: no GPU needed.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/sfs/SourceRing.h"
#include <future>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using kharvox::sfs::SourceRing;

static std::atomic<uintptr_t> nextHandle{0x1000};
template<class T> static T fresh() { return reinterpret_cast<T>(nextHandle.fetch_add(0x10)); }

static VKAPI_ATTR VkResult VKAPI_CALL fakeCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* out) { *out = fresh<VkImage>(); return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL fakeDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) {}
static VKAPI_ATTR void VKAPI_CALL fakeMemReqs(VkDevice, VkImage, VkMemoryRequirements* r) { r->size = 4096; r->alignment = 16; r->memoryTypeBits = 1; }
static VKAPI_ATTR VkResult VKAPI_CALL fakeAllocate(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* out) { *out = fresh<VkDeviceMemory>(); return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL fakeFree(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) {}
static VKAPI_ATTR VkResult VKAPI_CALL fakeBind(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; }
static VKAPI_ATTR VkResult VKAPI_CALL fakeCreateFence(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* out) { *out = fresh<VkFence>(); return VK_SUCCESS; }
static VKAPI_ATTR void VKAPI_CALL fakeDestroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}
static VKAPI_ATTR VkResult VKAPI_CALL fakeResetFences(VkDevice, uint32_t, const VkFence*) { return VK_SUCCESS; }
static std::atomic<bool> blockWait{false}, waitEntered{false};
static VKAPI_ATTR VkResult VKAPI_CALL fakeWaitFences(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) {
    waitEntered.store(true);
    while (blockWait.load()) std::this_thread::yield();
    return VK_SUCCESS;
}
static std::atomic<bool> blockSubmit{false}, submitEntered{false};
static VKAPI_ATTR VkResult VKAPI_CALL fakeSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence) {
    submitEntered.store(true);
    while (blockSubmit.load()) std::this_thread::yield();
    return VK_SUCCESS;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolver(VkDevice, const char* name) {
#define ENTRY(api, fn) if (!std::strcmp(name, #api)) return reinterpret_cast<PFN_vkVoidFunction>(&fn)
    ENTRY(vkCreateImage, fakeCreateImage); ENTRY(vkDestroyImage, fakeDestroyImage);
    ENTRY(vkGetImageMemoryRequirements, fakeMemReqs); ENTRY(vkAllocateMemory, fakeAllocate);
    ENTRY(vkFreeMemory, fakeFree); ENTRY(vkBindImageMemory, fakeBind);
    ENTRY(vkCreateFence, fakeCreateFence); ENTRY(vkDestroyFence, fakeDestroyFence);
    ENTRY(vkResetFences, fakeResetFences); ENTRY(vkWaitForFences, fakeWaitFences);
    ENTRY(vkQueueSubmit, fakeSubmit);
#undef ENTRY
    return nullptr;
}

static VkSwapchainCreateInfoKHR chainInfo(uint32_t images, VkSwapchainKHR old = VK_NULL_HANDLE) {
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.minImageCount = images; info.imageArrayLayers = 2;
    info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM; info.imageExtent = {64, 64};
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; info.oldSwapchain = old;
    return info;
}

static std::vector<VkImage> imagesOf(SourceRing& ring, VkSwapchainKHR chain) {
    uint32_t count = 0;
    assert(ring.enumerate(chain, &count, nullptr) == VK_SUCCESS);
    std::vector<VkImage> images(count);
    assert(ring.enumerate(chain, &count, images.data()) == VK_SUCCESS);
    return images;
}

static void exhaustedAcquireAllowsPresent(const VkPhysicalDeviceMemoryProperties& memory) {
    SourceRing ring;
    std::recursive_mutex queueMutex;
    assert(ring.initialize(reinterpret_cast<VkDevice>(1), reinterpret_cast<VkQueue>(2), &resolver, memory, &queueMutex));
    VkSwapchainKHR chain{};
    assert(ring.create(chainInfo(2), &chain) == VK_SUCCESS);
    std::array<uint32_t, 2> leased{};
    for (auto& index : leased) {
        assert(ring.acquire(chain, 0, fresh<VkSemaphore>(), VK_NULL_HANDLE, &index) == VK_SUCCESS);
    }
    std::promise<void> started;
    auto acquiring = std::async(std::launch::async, [&] {
        started.set_value();
        uint32_t index{};
        return ring.acquire(chain, 1000000000, fresh<VkSemaphore>(), VK_NULL_HANDLE, &index);
    });
    started.get_future().wait();
    const bool waiting = acquiring.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    auto presenting = std::async(std::launch::async, [&] {
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.swapchainCount = 1; present.pSwapchains = &chain; present.pImageIndices = &leased[0];
        return ring.present(reinterpret_cast<VkQueue>(2), present, false);
    });
    const bool progressed = presenting.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    const auto acquired = acquiring.get();
    const auto presented = presenting.get();
    ring.clearAfterDeviceIdle();
    assert(waiting);
    assert(progressed);
    assert(acquired == VK_SUCCESS);
    assert(presented == VK_SUCCESS);
}

static void submissionsUseSharedQueue(const VkPhysicalDeviceMemoryProperties& memory) {
    SourceRing ring;
    std::recursive_mutex queueMutex;
    assert(ring.initialize(reinterpret_cast<VkDevice>(1), reinterpret_cast<VkQueue>(2), &resolver, memory, &queueMutex));
    VkSwapchainKHR chain{};
    assert(ring.create(chainInfo(2), &chain) == VK_SUCCESS);
    uint32_t index{};
    std::unique_lock<std::recursive_mutex> queueLock(queueMutex);
    submitEntered.store(false);
    std::promise<void> acquireStarted;
    auto acquiring = std::async(std::launch::async, [&] {
        acquireStarted.set_value();
        return ring.acquire(chain, 1000000000, fresh<VkSemaphore>(), VK_NULL_HANDLE, &index);
    });
    acquireStarted.get_future().wait();
    const bool acquireBlocked = acquiring.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const bool acquireSubmitted = submitEntered.load();
    queueLock.unlock();
    const auto acquired = acquiring.get();
    queueLock.lock();
    submitEntered.store(false);
    std::promise<void> presentStarted;
    auto presenting = std::async(std::launch::async, [&] {
        presentStarted.set_value();
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.swapchainCount = 1; present.pSwapchains = &chain; present.pImageIndices = &index;
        return ring.present(reinterpret_cast<VkQueue>(2), present, false);
    });
    presentStarted.get_future().wait();
    const bool presentBlocked = presenting.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout;
    const bool presentSubmitted = submitEntered.load();
    queueLock.unlock();
    const auto presented = presenting.get();
    ring.clearAfterDeviceIdle();
    assert(acquireBlocked && !acquireSubmitted && acquired == VK_SUCCESS);
    assert(presentBlocked && !presentSubmitted && presented == VK_SUCCESS);
}

static void fenceWaitAllowsQueueUse(const VkPhysicalDeviceMemoryProperties& memory) {
    SourceRing ring;
    std::recursive_mutex queueMutex;
    assert(ring.initialize(reinterpret_cast<VkDevice>(1), reinterpret_cast<VkQueue>(2), &resolver, memory, &queueMutex));
    VkSwapchainKHR chain{};
    assert(ring.create(chainInfo(2), &chain) == VK_SUCCESS);
    for (unsigned n = 0; n < 2; ++n) {
        uint32_t index{};
        assert(ring.acquire(chain, 0, fresh<VkSemaphore>(), VK_NULL_HANDLE, &index) == VK_SUCCESS);
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.swapchainCount = 1; present.pSwapchains = &chain; present.pImageIndices = &index;
        assert(ring.present(reinterpret_cast<VkQueue>(2), present, false) == VK_SUCCESS);
    }
    blockWait.store(true); waitEntered.store(false);
    auto acquiring = std::async(std::launch::async, [&] {
        uint32_t index{};
        return ring.acquire(chain, 1000000000, fresh<VkSemaphore>(), VK_NULL_HANDLE, &index);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!waitEntered.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    const bool waiting = waitEntered.load();
    auto queueUser = std::async(std::launch::async, [&] {
        std::lock_guard<std::recursive_mutex> lock(queueMutex);
        return true;
    });
    const bool progressed = queueUser.wait_for(std::chrono::milliseconds(250)) == std::future_status::ready;
    blockWait.store(false);
    const auto acquired = acquiring.get();
    assert(queueUser.get());
    ring.clearAfterDeviceIdle();
    assert(waiting && progressed && acquired == VK_SUCCESS);
}

int main() {
    VkPhysicalDeviceMemoryProperties memory{};
    memory.memoryTypeCount = 1;
    memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    exhaustedAcquireAllowsPresent(memory);
    submissionsUseSharedQueue(memory);
    fenceWaitAllowsQueueUse(memory);

    SourceRing ring;
    assert(ring.initialize(reinterpret_cast<VkDevice>(1), reinterpret_cast<VkQueue>(2), &resolver, memory, nullptr));

    // Nothing owned yet; a null image is never owned.
    assert(!ring.ownsImage(VK_NULL_HANDLE));
    assert(!ring.ownsImage(fresh<VkImage>()));

    // Every image of a created chain is owned; foreign images are not.
    VkSwapchainKHR first{};
    assert(ring.create(chainInfo(3), &first) == VK_SUCCESS);
    const auto firstImages = imagesOf(ring, first);
    assert(firstImages.size() == 3);
    for (auto image : firstImages) assert(ring.ownsImage(image));
    const auto foreign = fresh<VkImage>();
    assert(!ring.ownsImage(foreign));
    assert(!ring.ownsImage(VK_NULL_HANDLE));

    // A recreated chain (old one retired but alive) is owned alongside it.
    VkSwapchainKHR second{};
    assert(ring.create(chainInfo(4, first), &second) == VK_SUCCESS);
    const auto secondImages = imagesOf(ring, second);
    assert(secondImages.size() == 4);
    for (auto image : firstImages) assert(ring.ownsImage(image));
    for (auto image : secondImages) assert(ring.ownsImage(image));

    // Destroying a chain withdraws exactly its images.
    assert(ring.destroy(first) == VK_SUCCESS);
    for (auto image : firstImages) assert(!ring.ownsImage(image));
    for (auto image : secondImages) assert(ring.ownsImage(image));

    // A failed create must not leak anything into the registry.
    VkSwapchainKHR bad{};
    auto rejected = chainInfo(3); rejected.imageArrayLayers = 1;
    assert(ring.create(rejected, &bad) == VK_ERROR_FEATURE_NOT_PRESENT);
    for (auto image : secondImages) assert(ring.ownsImage(image));

    // Readers running while chains are created and destroyed never crash and
    // always see a chain's images as owned for as long as that chain lives.
    VkSwapchainKHR pinned{};
    assert(ring.create(chainInfo(3), &pinned) == VK_SUCCESS);
    const auto pinnedImages = imagesOf(ring, pinned);
    std::atomic<bool> stop{false};
    std::atomic<long> wrong{0}, checks{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) readers.emplace_back([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            for (auto image : pinnedImages) if (!ring.ownsImage(image)) wrong.fetch_add(1);
            if (ring.ownsImage(foreign)) wrong.fetch_add(1);
            checks.fetch_add(1, std::memory_order_relaxed);
        }
    });
    while (checks.load() == 0) std::this_thread::yield();   // readers are running
    const long checksAtStart = checks.load();
    // At least 300 create/destroy cycles AND real overlap with the readers.
    for (int i = 0; i < 300 || (checks.load() - checksAtStart < 2000 && i < 200000); ++i) {
        VkSwapchainKHR churn{};
        assert(ring.create(chainInfo(2 + i % 4), &churn) == VK_SUCCESS);
        assert(ring.destroy(churn) == VK_SUCCESS);
    }
    stop.store(true);
    for (auto& r : readers) r.join();
    assert(wrong.load() == 0);
    assert(checks.load() - checksAtStart >= 2000);

    // A driver submission holding the main ring mutex must not block barrier
    // ownership queries. Release the fake driver before asserting on failure.
    blockSubmit.store(true); submitEntered.store(false);
    auto acquiring = std::async(std::launch::async, [&] {
        uint32_t index{};
        return ring.acquire(pinned, UINT64_MAX, fresh<VkSemaphore>(), VK_NULL_HANDLE, &index);
    });
    while (!submitEntered.load()) std::this_thread::yield();
    auto lookup = std::async(std::launch::async, [&] { return ring.ownsImage(pinnedImages[0]); });
    const bool independent = lookup.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    blockSubmit.store(false);
    assert(acquiring.get() == VK_SUCCESS);
    assert(lookup.get());
    assert(independent);

    // Device teardown drops everything.
    ring.clearAfterDeviceIdle();
    for (auto image : pinnedImages) assert(!ring.ownsImage(image));
    for (auto image : secondImages) assert(!ring.ownsImage(image));

    std::puts("sfs source ring registry tests passed");
    return 0;
}
