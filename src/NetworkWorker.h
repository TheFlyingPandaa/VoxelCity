#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <functional>
namespace vc {
// The one network worker is shared by traffic and city coverage. Jobs never capture live simulation objects.
class NetworkWorker {
public:
    using Cancellation=std::shared_ptr<std::atomic_bool>;
    static NetworkWorker& instance();
    static inline bool blocking=false; // Explicit drain mode for headless checks/startup only.
    template<class F> auto submit(F fn,Cancellation cancel={},bool waitForSlot=false) -> std::future<std::invoke_result_t<F>> {
        using T=std::invoke_result_t<F>;
        auto task=std::make_shared<std::packaged_task<T()>>([fn=std::move(fn),cancel]() mutable -> T {
            if(cancel&&cancel->load())return T{};
            return fn();
        });
        auto future=task->get_future();
        {std::unique_lock lock(mutex_);if(waitForSlot||blocking)cv_.wait(lock,[this]{return stop_||queue_.size()<64;});if(stop_||queue_.size()>=64)return {};queue_.push_back([task]{(*task)();});}
        cv_.notify_one();return future;
    }
    bool isWorkerThread() const {return std::this_thread::get_id()==thread_.get_id();}
    size_t pending() {std::lock_guard lock(mutex_);return queue_.size();}
    ~NetworkWorker();
private:
    NetworkWorker();
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
    bool stop_=false;
    std::jthread thread_;
};
template<class T> bool networkReady(std::future<T>& future,bool wait=false) {
    if(!future.valid())return false;
    if(wait||NetworkWorker::blocking){future.wait();return true;}
    return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;
}
struct NetworkLifetime {
    NetworkWorker::Cancellation cancelled=std::make_shared<std::atomic_bool>(false);
    ~NetworkLifetime(){cancelled->store(true);}
};
}
