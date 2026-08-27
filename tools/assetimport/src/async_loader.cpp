#include "jrpgmaker/assetimport/async_loader.hpp"

#include <condition_variable>
#include <type_traits>
#include <utility>

#include "jrpgmaker/assetimport/asset_import.hpp"

namespace jrpgmaker::assetimport {

AsyncLoader::AsyncLoader(std::size_t max_pending)
    : max_pending_(max_pending), worker_(std::make_unique<std::thread>([this] { WorkerLoop(); })) {}

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

bool AsyncLoader::Submit(const std::filesystem::path& path, MeshLoadedCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || queue_.size() + finished_.size() + (worker_busy_ ? 1u : 0u) >= max_pending_) {
            return false;
        }
        queue_.push_back(Request{path, std::move(callback)});
    }
    cv_.notify_one();
    return true;
}

bool AsyncLoader::SubmitTexture(const std::filesystem::path& path, TextureLoadedCallback callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || queue_.size() + finished_.size() + (worker_busy_ ? 1u : 0u) >= max_pending_) {
            return false;
        }
        queue_.push_back(Request{path, std::move(callback)});
    }
    cv_.notify_one();
    return true;
}

std::size_t AsyncLoader::Poll() {
    std::deque<Finished> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished.swap(finished_);
    }
    const std::size_t dispatched = finished.size();
    for (Finished& item : finished) {
        std::visit(
            [&item](auto& callback) {
                if (!callback) {
                    return;
                }
                using Callback = std::decay_t<decltype(callback)>;
                if constexpr (std::is_same_v<Callback, MeshLoadedCallback>) {
                    callback(std::move(item.path),
                             std::move(std::get<std::optional<core::MeshData>>(item.result)));
                } else {
                    callback(std::move(item.path),
                             std::move(std::get<TextureLoadResult>(item.result)));
                }
            },
            item.callback);
    }
    return dispatched;
}

std::size_t AsyncLoader::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() + finished_.size() + (worker_busy_ ? 1u : 0u);
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

        Finished finished{.path = std::move(request.path),
                          .result = std::optional<core::MeshData>{},
                          .callback = std::move(request.callback)};
        std::visit(
            [&finished](auto& callback) {
                using Callback = std::decay_t<decltype(callback)>;
                GltfLoadError error;
                if constexpr (std::is_same_v<Callback, MeshLoadedCallback>) {
                    finished.result = LoadGltfMesh(finished.path, &error);
                } else {
                    finished.result =
                        TextureLoadResult{.texture = LoadTextureFile(finished.path, &error),
                                          .error = std::move(error.message)};
                }
            },
            finished.callback);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            worker_busy_ = false;
            finished_.push_back(std::move(finished));
        }
    }
}

} // namespace jrpgmaker::assetimport
