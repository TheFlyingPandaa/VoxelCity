#include "RenderWorker.h"
#include <stdexcept>
#include <algorithm>
namespace vc {
namespace {
void pumpMessages() {
    MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
        if(msg.message==WM_QUIT){PostQuitMessage(int(msg.wParam));break;}
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }
}
}
RenderWorker::RenderWorker(HWND window,unsigned w,unsigned h,bool debug,ImFontAtlas* fonts,unsigned delayMs):width_(w),height_(h),delayMs_(delayMs),thread_([this,window,debug,fonts]{run(window,debug,fonts);}) {
    waitUntil([this]{return ready_;});poll();
}
RenderWorker::~RenderWorker() {
    // Keep servicing the window while DXGI finishes presentation.
    try{waitIdle();}catch(...){}
    {std::lock_guard lock(mutex_);stop_=true;}cv_.notify_all();
    for(;;){{std::unique_lock lock(mutex_);if(finished_)break;cv_.wait_for(lock,std::chrono::milliseconds(1));}pumpMessages();}
    if(thread_.joinable())thread_.join();
}
void RenderWorker::waitUntil(const std::function<bool()>& condition) {
    for(;;) {
        {std::unique_lock lock(mutex_);if(error_)std::rethrow_exception(error_);if(condition())return;
         cv_.wait_for(lock,std::chrono::milliseconds(1));}
        pumpMessages();
    }
}
void RenderWorker::poll() {
    std::lock_guard lock(mutex_);if(error_)std::rethrow_exception(error_);
    stats=completed_;adapterName=adapter_;presented=presented_;dropped=dropped_;renderMs=renderMs_;
}
void RenderWorker::waitIdle(){
    waitUntil([this]{return !pending_&&!busy_;});poll();
    std::lock_guard lock(mutex_);
    if(!presentationTimes_.empty()){double total=0;for(auto t:presentationTimes_)total+=t;frameAverageMs=total/presentationTimes_.size();auto sorted=presentationTimes_;std::sort(sorted.begin(),sorted.end());frameP95Ms=sorted[(sorted.size()-1)*95/100];}
    if(!renderTimes_.empty()){double total=0;for(auto t:renderTimes_)total+=t;renderAverageMs=total/renderTimes_.size();}
}
void RenderWorker::render(World& world,const View& view,bool vsync,const std::filesystem::path& capture,std::span<const CarInstance> cars) {
    auto start=std::chrono::steady_clock::now();poll();
    if(!snapshot_||snapshot_->railRevision()!=world.railRevision()||snapshot_->topologyRevision()!=world.topologyRevision()||snapshot_->replacementRevision()!=world.replacementRevision()||snapshot_->parcelRevision()!=world.parcelRevision()||snapshot_->styleRevision()!=world.styleRevision()||snapshot_->vegetationRevision()!=world.vegetationRevision()) {
        snapshot_=std::make_shared<World>(world);
    }
    Packet p;p.world=snapshot_;p.view=view;p.vsync=vsync;p.width=width_;p.height=height_;p.capture=capture;p.generation=generation_;p.cars.assign(cars.begin(),cars.end());
    if(auto* draw=ImGui::GetDrawData();draw&&view.showUI) {
        p.ui.TotalVtxCount=draw->TotalVtxCount;p.ui.TotalIdxCount=draw->TotalIdxCount;p.ui.CmdListsCount=draw->CmdListsCount;
        p.ui.DisplayPos=draw->DisplayPos;p.ui.DisplaySize=draw->DisplaySize;p.ui.FramebufferScale=draw->FramebufferScale;
        for(auto* list:draw->CmdLists) {
            UiDrawList copy;copy.VtxBuffer.assign(list->VtxBuffer.begin(),list->VtxBuffer.end());copy.IdxBuffer.assign(list->IdxBuffer.begin(),list->IdxBuffer.end());copy.CmdBuffer.assign(list->CmdBuffer.begin(),list->CmdBuffer.end());
            for(auto& cmd:copy.CmdBuffer)if(cmd.UserCallback&&cmd.UserCallback!=ImDrawCallback_ResetRenderState)throw std::runtime_error("UI draw callbacks cannot cross threads");
            p.ui.CmdLists.push_back(std::move(copy));
        }
    }
    // Captures are reliable; ordinary frames use latest-wins backpressure.
    waitUntil([this]{return !pending_||pending_->capture.empty();});
    {std::lock_guard lock(mutex_);if(pending_)++dropped_;pending_=std::move(p);}cv_.notify_all();
    snapshotMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    if(!capture.empty())waitIdle();
}
void RenderWorker::run(HWND window,bool debug,ImFontAtlas* fonts) {
    SetThreadDescription(GetCurrentThread(),L"VoxelCity Rendering");
    try {
        Renderer renderer(window,width_,height_,debug,fonts);
        unsigned w=width_,h=height_;
        std::shared_ptr<const World> source;uint64_t generation=~uint64_t(0);
        auto lastPresent=std::chrono::steady_clock::now();
        std::unique_ptr<World> visuals;
        {std::lock_guard lock(mutex_);adapter_=renderer.adapterName;ready_=true;}cv_.notify_all();
        for(;;) {
            std::optional<Packet> packet;
            {std::unique_lock lock(mutex_);cv_.wait(lock,[this]{return stop_||pending_.has_value();});if(stop_&&!pending_)break;packet=std::move(pending_);pending_.reset();busy_=true;}cv_.notify_all();
            auto& p=*packet;auto start=std::chrono::steady_clock::now();
            if(p.width!=w||p.height!=h){w=p.width;h=p.height;renderer.resize(w,h);}
            if(delayMs_)std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
            if(generation!=p.generation){if(source)renderer.invalidateWorld();source.reset();generation=p.generation;}
            if(source!=p.world) {
                // Compute dirtiness against the last consumed snapshot, not the last submitted frame.
                auto dirty=p.world->changedVisualChunks(source.get());
                visuals=std::make_unique<World>(*p.world);visuals->takeDirty();visuals->markVisualChunks(dirty);source=p.world;
            }
            renderer.render(*visuals,p.view,p.vsync,p.capture,p.cars,&p.ui);
            auto now=std::chrono::steady_clock::now();
            {std::lock_guard lock(mutex_);completed_=renderer.stats;++presented_;renderMs_=std::chrono::duration<double,std::milli>(now-start).count();
             if(presented_>30){if(presentationTimes_.size()==4096){presentationTimes_.erase(presentationTimes_.begin(),presentationTimes_.begin()+2048);renderTimes_.erase(renderTimes_.begin(),renderTimes_.begin()+2048);}presentationTimes_.push_back(std::chrono::duration<double,std::milli>(now-lastPresent).count());renderTimes_.push_back(renderMs_);}busy_=false;}
            lastPresent=now;cv_.notify_all();
        }
        renderer.waitIdle();
    } catch(...) {{std::lock_guard lock(mutex_);error_=std::current_exception();busy_=false;ready_=true;}cv_.notify_all();}
    {std::lock_guard lock(mutex_);finished_=true;}cv_.notify_all();
}
}
