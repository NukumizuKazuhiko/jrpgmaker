#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "jrpgmaker/core/asset.hpp"
#include "jrpgmaker/core/mesh.hpp"

namespace jrpgmaker::assetimport {

// Completion callback for an async mesh load. The registry is guaranteed to be
// stable (no concurrent mutation) while this runs because AsyncLoader::Poll is
// the only path that dispatches callbacks and is called from one thread.
using MeshLoadedCallback =
    std::function<void(std::filesystem::path path, std::optional<core::MeshData> mesh)>;

// Background mesh loader (P2 async asset system). The worker thread only parses
// glTF (LoadGltfMesh is a pure function); results are handed back to the
// caller's thread via Poll so the core AssetRegistry never sees concurrent
// access and needs no locking.
//
// v0 model:
//   - Submit queues a load request and returns immediately.
//   - The worker thread parses the file in the background.
//   - Poll() (call from your main/update loop) invokes callbacks for every
//     finished request, in submission order, on the calling thread.
// A request that fails still produces a callback (mesh == nullopt) so the
// caller can surface the error; the loader never throws.
class AsyncLoader {
public:
    AsyncLoader();
    ~AsyncLoader();

    AsyncLoader(const AsyncLoader&) = delete;
    AsyncLoader& operator=(const AsyncLoader&) = delete;

    // Queues a mesh load. `callback` is invoked from Poll on completion.
    void Submit(const std::filesystem::path& path, MeshLoadedCallback callback);

    // Runs all finished callbacks (submission order) on the calling thread.
    // Returns the number of callbacks dispatched.
    std::size_t Poll();

    // Number of requests still being parsed (queued + in flight).
    std::size_t pending_count() const;

private:
    struct Request {
        std::filesystem::path path;
        MeshLoadedCallback callback;
    };

    struct Finished {
        std::filesystem::path path;
        std::optional<core::MeshData> mesh;
        MeshLoadedCallback callback;
    };

    void WorkerLoop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Request> queue_;
    std::deque<Finished> finished_;
    bool stop_ = false;
    bool worker_busy_ = false;
    std::unique_ptr<std::thread> worker_;
};

} // namespace jrpgmaker::assetimport