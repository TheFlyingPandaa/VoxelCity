#pragma once
#include "Renderer.h"
#include "UiRenderer.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <optional>
#include <functional>
namespace vc {
// Main-thread facade. All Renderer/DX12 objects belong to thread_.
class RenderWorker {
public:
    RenderWorker(HWND,unsigned,unsigned,bool,ImFontAtlas*,unsigned delayMs=0);
    ~RenderWorker();
    void resize(unsigned w,unsigned h) { width_=w;height_=h; }
    void render(World&,const View&,bool,const std::filesystem::path&,std::span<const CarInstance>);
    void poll();
    void invalidateWorld(){snapshot_.reset();++generation_;}
    void waitIdle();
    RenderStats stats;
    std::string adapterName;
    uint64_t presented=0,dropped=0;
    double renderMs=0,snapshotMs=0,frameAverageMs=0,frameP95Ms=0,renderAverageMs=0;
private:
    struct Packet {
        std::shared_ptr<const World> world;
        View view; bool vsync; unsigned width,height;
        std::filesystem::path capture;
        std::vector<CarInstance> cars;
        UiDrawData ui;
        uint64_t generation=0;
    };
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Packet> pending_;
    bool ready_=false,busy_=false,stop_=false,finished_=false;
    std::exception_ptr error_;
    RenderStats completed_;
    std::string adapter_;
    uint64_t presented_=0,dropped_=0;
    double renderMs_=0;
    uint64_t generation_=0;
    std::vector<double> presentationTimes_,renderTimes_;
    unsigned width_,height_;
    const unsigned delayMs_;
    std::shared_ptr<const World> snapshot_;
    std::jthread thread_;
    void run(HWND,bool,ImFontAtlas*);
    void waitUntil(const std::function<bool()>&);
};
}
