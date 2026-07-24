#pragma once
#include <atomic>
#include <memory>

namespace openlyrics {

class CancelToken {
public:
    CancelToken() : cancelled_(false) {}
    void cancel() { cancelled_.store(true, std::memory_order_release); }
    bool isCancelled() const { return cancelled_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> cancelled_;
    CancelToken(const CancelToken&) = delete;
    CancelToken& operator=(const CancelToken&) = delete;
};

}  // namespace openlyrics
