#include "jrpgmaker/assetimport/async_loader.hpp"

#include <condition_variable>
#include <utility>

#include "jrpgmaker/assetimport/asset_import.hpp"

namespace jrpgmaker::assetimport {

AsyncLoader::AsyncLoader() : worker_(std::make_unique<std::thread>([this] { WorkerLoop(); })) {}

AsyncLoader::~AsyncLoader() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_ != nullptr && worker_->joinable()) {
        worker_->join();
    }
    // Any requests still queued never completed; drop them (their callbacks are
    // destroyed without firing). In-flight parsing completes but its result is
    // discarded if it lands after shutdown.
}

void AsyncLoader::Submit(const std::filesystem::path& path, MeshLoadedCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(Request{path, std::move(callback)});
    }
    cv_.notify_one();
}

std::size_t AsyncLoader::Poll() {
    std::deque<Finished> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished.swap(finished_);
    }
    for (Finished& item : finished) {
        if (item.callback) {
            item.callback(std::move(item.path), std::move(item.mesh));
        }
    }
    return finished.size();
}

std::size_t AsyncLoader::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + (worker_busy_ ? 1u : 0u);
}

void AsyncLoader::WorkerLoop() {
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) {
                break;
            }
            request = std::move(queue_.front());
            queue_.pop_front();
            worker_busy_ = true;
        }

        GltfLoadError error;
        std::optional<core::MeshData> mesh = LoadGltfMesh(request.path, &error);
        if (!mesh.has_value()) {
            // Surface the parse failure via the callback's nullopt path.
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            worker_busy_ = false;
            finished_.push_back(
                Finished{std::move(request.path), std::move(mesh), std::move(request.callback)});
        }
    }
}

} // namespace jrpgmaker::assetimport