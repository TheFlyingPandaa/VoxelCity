#include "NetworkWorker.h"
#include <windows.h>
namespace vc {
NetworkWorker& NetworkWorker::instance(){static NetworkWorker worker;return worker;}
NetworkWorker::NetworkWorker():thread_([this]{
    SetThreadDescription(GetCurrentThread(),L"VoxelCity Pathfinding");
    for(;;){std::function<void()> job;{std::unique_lock lock(mutex_);cv_.wait(lock,[this]{return stop_||!queue_.empty();});if(stop_)return;job=std::move(queue_.front());queue_.pop_front();}cv_.notify_all();job();}
}){}
NetworkWorker::~NetworkWorker(){{std::lock_guard lock(mutex_);stop_=true;queue_.clear();}cv_.notify_all();thread_.join();}
}
