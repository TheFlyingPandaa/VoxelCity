#include "Renderer.h"
#include "RenderWorker.h"
#include "City.h"
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <optional>
#include <random>
#include <stdexcept>
#include <sstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace vc {
using namespace DirectX;
struct Camera {
    XMFLOAT3 focus{WorldSize/2.f+TileSize*3,0,WorldSize/2.f+TileSize*3};
    float yaw=0.65f,pitch=0.86f,distance=TileSize*26.25f;
    View view(unsigned width,unsigned height) const {
        XMVECTOR target=XMLoadFloat3(&focus);
        XMVECTOR eye=target+XMVectorSet(std::sin(yaw)*std::cos(pitch)*distance,std::sin(pitch)*distance,std::cos(yaw)*std::cos(pitch)*distance,0);
        XMMATRIX v=XMMatrixLookAtLH(eye,target,XMVectorSet(0,1,0,0));
        XMMATRIX projection=XMMatrixPerspectiveFovLH(XM_PIDIV4,float(width)/float(height),0.5f,WorldSize*4.f);
        View result{};XMStoreFloat4x4(&result.viewProjection,v*projection);XMStoreFloat3(&result.eye,eye);v.r[3]=XMVectorSet(0,0,0,1);XMStoreFloat4x4(&result.rayInverse,XMMatrixInverse(nullptr,v*projection));return result;
    }
    Cell pick(float mx,float my,unsigned width,unsigned height) const {
        View v=view(width,height);XMMATRIX inverse=XMMatrixInverse(nullptr,XMLoadFloat4x4(&v.viewProjection));
        float x=2*mx/float(width)-1,y=1-2*my/float(height);
        XMVECTOR nearPoint=XMVector3TransformCoord(XMVectorSet(x,y,0,1),inverse);
        XMVECTOR farPoint=XMVector3TransformCoord(XMVectorSet(x,y,1,1),inverse);
        XMFLOAT3 a,d;XMStoreFloat3(&a,nearPoint);XMStoreFloat3(&d,farPoint-nearPoint);
        if(d.y>=-0.00001f) return {};
        float t=-a.y/d.y;
        return t>=0?World::cellAt(a.x+d.x*t,a.z+d.z*t):Cell{};
    }
    void input(const ImGuiIO& io) {
        if(io.WantCaptureMouse) return;
        distance=std::clamp(distance*std::pow(0.85f,io.MouseWheel),18.f,WorldSize*1.91f);
        if(io.MouseDown[2]) {
            if(io.KeyAlt) {yaw-=io.MouseDelta.x*0.006f;pitch=std::clamp(pitch+io.MouseDelta.y*0.006f,0.25f,1.48f);}
            else {
                float scale=distance*0.0009f;
                focus.x+=scale*(std::cos(yaw)*io.MouseDelta.x-std::sin(yaw)*io.MouseDelta.y);
                focus.z+=scale*(-std::sin(yaw)*io.MouseDelta.x-std::cos(yaw)*io.MouseDelta.y);
                focus.x=std::clamp(focus.x,0.f,float(WorldSize));focus.z=std::clamp(focus.z,0.f,float(WorldSize));
            }
        }
    }
};
struct Application {
    unsigned width=1600,height=900;
    bool minimized=false,resizePending=false,active=true,closeRequested=false;
    std::unique_ptr<RenderWorker> renderer;
    HWND window=nullptr;
    ~Application() {
        // Also covers worker/startup exceptions before normal run() cleanup.
        renderer.reset();
        if(ImGui::GetCurrentContext()){ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();}
        if(window&&IsWindow(window))DestroyWindow(window);
    }
};
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE) {app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(ImGui::GetCurrentContext()) ImGui_ImplWin32_WndProcHandler(window,message,w,l);
    if(app) switch(message) {
        case WM_SIZE: app->minimized=w==SIZE_MINIMIZED; if(!app->minimized) {app->width=LOWORD(l);app->height=HIWORD(l);app->resizePending=true;}return 0;
        case WM_ACTIVATEAPP: app->active=w!=0;return 0;
        case WM_GETMINMAXINFO: {auto* size=reinterpret_cast<MINMAXINFO*>(l);size->ptMinTrackSize={900,800};return 0;}
        case WM_CLOSE: app->closeRequested=true;return 0;
        case WM_DESTROY: PostQuitMessage(0);return 0;
        case WM_SYSCHAR: return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
static std::optional<std::filesystem::path> mapDialog(HWND owner,bool save) {
    wchar_t file[32768]=L"City.vcity";
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=owner;dialog.lpstrFilter=L"VoxelCity maps (*.vcity)\0*.vcity\0\0";
    dialog.lpstrFile=file;dialog.nMaxFile=32768;dialog.lpstrDefExt=L"vcity";
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    BOOL result=save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    if(result) return std::filesystem::path(file);
    if(CommDlgExtendedError()!=0) throw std::runtime_error("Could not open the file dialog.");return {};
}
static void style() {
    ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();
    s.WindowRounding=10;s.FrameRounding=5;s.GrabRounding=4;s.WindowPadding={18,16};s.FramePadding={12,7};s.ItemSpacing={10,10};
    s.Colors[ImGuiCol_WindowBg]={0.055f,0.075f,0.085f,0.96f};
    s.Colors[ImGuiCol_Button]={0.12f,0.24f,0.26f,1};s.Colors[ImGuiCol_ButtonHovered]={0.18f,0.38f,0.39f,1};
    s.Colors[ImGuiCol_CheckMark]={0.4f,0.87f,0.75f,1};s.Colors[ImGuiCol_Header]={0.12f,0.24f,0.26f,1};
    const char* font="C:/Windows/Fonts/segoeui.ttf";
    if(std::filesystem::exists(font)) ImGui::GetIO().Fonts->AddFontFromFileTTF(font,18);
}
struct Options {
    int frames=0,scenario=0,cars=50000,trafficWarmup=0;
    uint32_t trafficSeed=std::random_device{}();
    bool trafficBenchmark=false,cityMode=true,cityInput=false;
    int cityScenario=-1,cityStress=0,citySpeed=1,quality=2;
    bool threadingTest=false,railwayScenario=false;
    bool voxels=true,cameraPath=false,noUI=false,noGrid=false;
    bool debug=false,benchmark=false,overview=false,edit=false,windowTest=false,interactionTest=false,closeUp=false,noShadows=false;
    unsigned width=1600,height=900;
    std::optional<Camera> captureCamera;
    std::filesystem::path capture,report,captureSequence;
};
static Options options() {
    Options o;int count=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&count);
    for(int i=1;i<count;++i) {
        std::wstring a=args[i];
        auto next=[&]()->std::wstring {if(i+1>=count) throw std::runtime_error("Missing command line option value.");return args[++i];};
        if(a==L"--city-input-test") {o.cityInput=true;o.cityMode=true;o.frames=90;}
        else if(a==L"--railway-scenario") {o.railwayScenario=true;o.cityMode=true;}
        else if(a==L"--threading-test") {o.threadingTest=true;o.cityInput=true;o.cityMode=true;o.frames=90;}
        else if(a==L"--renderer") {auto v=next();if(v!=L"voxel"&&v!=L"legacy")throw std::runtime_error("Renderer must be voxel or legacy");o.voxels=v==L"voxel";}
        else if(a==L"--quality") {auto v=next();o.quality=v==L"low"?0:v==L"medium"?1:v==L"high"?2:-1;if(o.quality<0)throw std::runtime_error("Quality must be low, medium or high");}
        else if(a==L"--camera-path") o.cameraPath=true;
        else if(a==L"--camera-view") {
            Camera c;c.focus.x=std::stof(next());c.focus.y=std::stof(next());c.focus.z=std::stof(next());
            c.yaw=std::stof(next());c.pitch=std::stof(next());c.distance=std::stof(next());
            if(!std::isfinite(c.focus.x)||!std::isfinite(c.focus.y)||!std::isfinite(c.focus.z)||
               !std::isfinite(c.yaw)||!std::isfinite(c.pitch)||!std::isfinite(c.distance)||
               c.focus.x<0||c.focus.x>WorldSize||c.focus.z<0||c.focus.z>WorldSize||
               c.pitch<.25f||c.pitch>1.48f||c.distance<18||c.distance>WorldSize*1.91f)
                throw std::runtime_error("Invalid camera view: use world focus XYZ, yaw/pitch radians and distance within camera limits.");
            o.captureCamera=c;
        }
        else if(a==L"--no-ui") o.noUI=true;
        else if(a==L"--no-grid") o.noGrid=true;
        else if(a==L"--debug-layer") o.debug=true;
        else if(a==L"--benchmark") {o.benchmark=true;o.width=2560;o.height=1440;}
        else if(a==L"--cars") {o.cars=std::stoi(next());o.cityMode=false;}
        else if(a==L"--city-scenario") {auto v=next();o.cityScenario=v==L"starter"?0:v==L"town"?500:v==L"metropolis"?10000:-2;if(o.cityScenario==-2)throw std::runtime_error("City scenario must be starter, town, or metropolis");o.cityMode=true;}
        else if(a==L"--traffic-seed") o.trafficSeed=uint32_t(std::stoul(next()));
        else if(a==L"--traffic-warmup") o.trafficWarmup=std::stoi(next());
        else if(a==L"--traffic-benchmark") o.trafficBenchmark=true;
        else if(a==L"--city-stress") {o.cityStress=std::stoi(next());if(o.cityStress<1||o.cityStress>10000)throw std::runtime_error("City stress target must be 1..10000");o.cityMode=true;o.cityScenario=10000;}
        else if(a==L"--city-speed") {o.citySpeed=std::stoi(next());if(o.citySpeed<1||o.citySpeed>3)throw std::runtime_error("City speed must be 1..3");}
        else if(a==L"--frames") o.frames=std::stoi(next());
        else if(a==L"--scenario") {o.cityMode=false;auto s=next();if(s==L"empty") o.scenario=0;else if(s==L"network") o.scenario=1;else if(s==L"full") o.scenario=2;else if(s==L"showcase") o.scenario=3;else if(s==L"roundabout") o.scenario=4;else if(s==L"diagonal") o.scenario=5;else throw std::runtime_error("Scenario must be empty, network, full, showcase, roundabout, or diagonal.");}
        else if(a==L"--capture") o.capture=next();
        else if(a==L"--capture-sequence") o.captureSequence=next();
        else if(a==L"--report") o.report=next();
        else if(a==L"--overview") o.overview=true;
        else if(a==L"--close-up") o.closeUp=true;
        else if(a==L"--no-shadows") o.noShadows=true;
        else if(a==L"--interaction-test") {o.interactionTest=true;o.frames=90;o.scenario=0;o.cityMode=false;}
        else if(a==L"--edit-stress") o.edit=true;
        else if(a==L"--window-test") o.windowTest=true;
        else if(a==L"--smoke-test") {o.frames=90;o.debug=true;o.scenario=1;o.cityMode=false;}
        else throw std::runtime_error("Unknown command line argument.");
    }
    LocalFree(args);if(o.cars<0 || o.cars>100000 || o.trafficWarmup<0)throw std::runtime_error("Cars must be 0..100000 and traffic warmup nonnegative.");if(o.frames<0) throw std::runtime_error("Frame count cannot be negative.");return o;
}
int run(HINSTANCE instance) {
    Options config=options();Application app;app.width=config.width;app.height=config.height;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.lpszClassName=L"VoxelCityWindow";
    if(!RegisterClassExW(&wc)) throw std::runtime_error("Could not register window class.");
    RECT bounds{0,0,LONG(app.width),LONG(app.height)};AdjustWindowRectEx(&bounds,WS_OVERLAPPEDWINDOW,FALSE,0);
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"VoxelCity | City Builder",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,&app);
    if(!window) throw std::runtime_error("Could not create application window.");
    app.window=window;
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
    ImGui_ImplWin32_Init(window);ImGui::GetIO().BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
    style();
    unsigned char* fontPixels;int fontWidth,fontHeight;ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&fontPixels,&fontWidth,&fontHeight);
    app.renderer=std::make_unique<RenderWorker>(window,app.width,app.height,config.debug,ImGui::GetIO().Fonts,config.threadingTest?40:0);
    ShowWindow(window,SW_SHOW);UpdateWindow(window);
    NetworkWorker::blocking=true;
    auto world=std::make_unique<World>();world->generateScenario(config.scenario);
    TrafficSimulation traffic({config.cityMode?0:size_t(config.cars),config.trafficSeed,20000,64,config.cityMode});
    CitySimulation city(config.trafficSeed);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    auto dataPath=std::filesystem::path(executable).parent_path()/"data"/"buildings.csv";
    if(config.cityMode)city.loadDefinitions(dataPath);
    if(config.cityMode){if(config.railwayScenario)city.railwayScenario(*world,traffic);else if(config.cityScenario>=0)city.scenario(*world,traffic,config.cityScenario);else city.newCity(*world,traffic);}
    const size_t startingRoadCount=world->roadCount();
    if(config.cityStress)city.prepareTrafficStress(*world);city.speed=config.citySpeed;
    constexpr int ToolBulldoze=100,ToolRoadRule=101,ToolRoundabout=102,ToolInterchange=103,ToolRail=104,ToolRailOverpass=105;
    int railRotation=0;
    int cityTool=0,ruleDirection=0,overlay=0;RoadClass selectedRoad=RoadClass::Street;bool signal=false,showBudget=false,guide=true;
    ImVec2 residentialButton{},powerButton{},roadButton{};
    if(config.cityInput){city.paused=true;ImGui::GetIO().ConfigInputTrickleEventQueue=false;}
    Cell selected{};uint64_t savedRevision=city.revision;
    double autosaveElapsed=0;unsigned autosaveSlot=0;
    std::filesystem::path autosaveFolder;
    if(config.cityMode){wchar_t local[32768]{};if(GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))autosaveFolder=std::filesystem::path(local)/"VoxelCity"/"Autosaves";}

    const auto rampStart=std::chrono::steady_clock::now();
    for(int i=0;i<config.trafficWarmup;++i){if(config.cityMode){if(config.cityStress)city.replenishTrafficStress(traffic,size_t(config.cityStress));city.tick(*world,traffic);}else traffic.tick(*world);}
    if(config.cityStress){int ticks=0;while(traffic.stats().active<size_t(config.cityStress)&&ticks++<30000){city.replenishTrafficStress(traffic,size_t(config.cityStress));city.tick(*world,traffic);}if(traffic.stats().active<size_t(config.cityStress))throw std::runtime_error("Stress fixture did not reach its active vehicle target");}
    double trafficWarmupSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-rampStart).count();
    Camera camera;if(config.overview) {camera.focus={WorldSize/2.f,0,WorldSize/2.f};camera.distance=WorldSize*1.86f;}if(config.closeUp) camera.distance=TileSize*8.125f;
    if(config.cityMode&&!config.cityInput&&config.cityScenario<0){camera.focus={HighwayJunctionX*float(TileSize),0,(HighwayEastRow+6)*float(TileSize)};camera.distance=TileSize*45;}
    if(config.cityMode&&config.cityScenario>=0){camera.focus={248.f*TileSize,0,225.f*TileSize};camera.distance=TileSize*45;}
    if(config.overview){camera.focus={WorldSize/2.f,0,WorldSize/2.f};camera.distance=WorldSize*1.86f;}
    else if(config.closeUp)camera.distance=TileSize*8.125f;
    if(config.railwayScenario){camera.focus={248.f*TileSize,0,208.f*TileSize};camera.distance=TileSize*43;}
    if(config.captureCamera)camera=*config.captureCamera;
    if(config.interactionTest) ImGui::GetIO().ConfigInputTrickleEventQueue=false;
    bool grid=!config.noGrid&&!config.voxels,shadows=!config.noShadows,vsync=!config.benchmark,diagnostics=config.benchmark,unsaved=false;
    Cell roadStart{};Cell previous{};int previousButton=-1;std::string status="Ready to build. Drag a line and release to build your first road.";
    std::vector<double> mainWorkTimes,cpuTimes,gpuTimes,trafficTimes,routingTimes,sceneTimes,surfaceTimes,lightTimes,resolveTimes;size_t minimumCars=SIZE_MAX,maximumCars=0;int frame=0;bool quit=false;
    NetworkWorker::blocking=false;
    using Clock=std::chrono::steady_clock;auto last=Clock::now(),start=last;double maxFrame=0;
    while(!quit) {
        if(vsync&&!config.frames){auto due=last+std::chrono::microseconds(8333);auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(due-Clock::now()).count();if(remaining>0)MsgWaitForMultipleObjectsEx(0,nullptr,DWORD(remaining),QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {if(msg.message==WM_QUIT) quit=true;TranslateMessage(&msg);DispatchMessageW(&msg);}if(quit) break;
        if(app.minimized) {WaitMessage();last=Clock::now();continue;}
        if(app.resizePending) {app.renderer->resize(app.width,app.height);app.resizePending=false;}
        auto now=Clock::now();double elapsed=std::chrono::duration<double,std::milli>(now-last).count();last=now;
        if(frame>=30) {cpuTimes.push_back(elapsed);gpuTimes.push_back(app.renderer->stats.gpuMs);sceneTimes.push_back(app.renderer->stats.voxelSceneMs);surfaceTimes.push_back(app.renderer->stats.voxelSurfaceMs);lightTimes.push_back(app.renderer->stats.voxelLightMs);resolveTimes.push_back(app.renderer->stats.voxelResolveMs);maxFrame=std::max(maxFrame,elapsed);}
        app.renderer->poll();ImGui_ImplWin32_NewFrame();
        if(config.interactionTest) {
            auto& testIO=ImGui::GetIO();
            Cell target=frame<12?Cell{259,259}:frame<14?Cell{265,259}:Cell{262,259};
            auto v=camera.view(app.width,app.height);
            auto p=XMVector3TransformCoord(XMVectorSet(float(target.x*TileSize+TileSize/2),0,float(target.z*TileSize+TileSize/2),1),XMLoadFloat4x4(&v.viewProjection));
            float mx=(XMVectorGetX(p)+1)*float(app.width)*0.5f,my=(1-XMVectorGetY(p))*float(app.height)*0.5f;
            if(frame>=20) {mx=100;my=200;}
            testIO.AddMousePosEvent(mx,my);testIO.AddMouseButtonEvent(0,(frame>=11 && frame<=12) || (frame>=21 && frame<=23));
            testIO.AddMouseButtonEvent(1,frame==15);app.active=true;
        }

        if(config.cityInput){
            auto& testIO=ImGui::GetIO();Cell target=frame>=56?Cell{268,261}:frame>=53?Cell{263,256}:frame<12?Cell{259,259}:frame<20?Cell{265,259}:Cell{261,258};
            auto v=camera.view(app.width,app.height);auto p=XMVector3TransformCoord(XMVectorSet(float(target.x*TileSize+8),0,float(target.z*TileSize+8),1),XMLoadFloat4x4(&v.viewProjection));
            ImVec2 mouse{(XMVectorGetX(p)+1)*app.width*.5f,(1-XMVectorGetY(p))*app.height*.5f};
            if(frame>=19&&frame<=21)mouse=residentialButton;if(frame>=31&&frame<=33)mouse=powerButton;
            if(frame==12){auto bend=XMVector3TransformCoord(XMVectorSet(262.f*TileSize+8,0,257.f*TileSize+8,1),XMLoadFloat4x4(&v.viewProjection));mouse={(XMVectorGetX(bend)+1)*app.width*.5f,(1-XMVectorGetY(bend))*app.height*.5f};}
            if(frame>=40&&frame<50||frame>=59)mouse={100,180};if(frame>=50&&frame<=52)mouse=roadButton;
            testIO.AddMousePosEvent(mouse.x,mouse.y);testIO.AddMouseButtonEvent(0,frame==11||frame==12||frame==20||frame==25||frame==32||frame==36||frame==50||frame==55||frame==56);testIO.AddMouseButtonEvent(1,frame==28);app.active=true;
        }
        ImGui::NewFrame();auto& io=ImGui::GetIO();
        camera.input(io);Cell hover=camera.pick(io.MousePos.x,io.MousePos.y,app.width,app.height);
        if(io.WantCaptureMouse || !app.active || io.MousePos.x<0 || io.MousePos.y<0 || io.MousePos.x>=app.width || io.MousePos.y>=app.height) hover={};
        int button=io.MouseDown[1]?1:io.MouseDown[0]?0:-1;
        if(button>=0 && hover.x>=0 && !io.MouseDown[2]) {
            Cell from=previous.x>=0&&previousButton==button?previous:hover;
            if(config.cityMode){
                if(button==1||cityTool==ToolBulldoze){if(hover!=previous||button!=previousButton){if(cityTool==0&&world->road(hover.x,hover.z)&&world->hasRail(hover)&&!city.at(hover))city.buildRoad(*world,hover,hover,true,status);else city.bulldoze(*world,hover,status);}}
                else if(cityTool==0||cityTool==ToolRail){if(ImGui::IsMouseClicked(0))roadStart=hover;}
                else if(cityTool==ToolRailOverpass){if(ImGui::IsMouseClicked(0))city.buildRailOverpass(*world,hover,status);}
                else if(cityTool>0&&cityTool<int(BuildingKind::Count)&&railFacility(BuildingKind(cityTool))){if(ImGui::IsMouseClicked(0))city.placeRailFacility(*world,hover,BuildingKind(cityTool),railRotation,status);}
                else if(cityTool==ToolRoundabout){if(ImGui::IsMouseClicked(0))city.buildRoundabout(*world,hover,status);}
                else if(cityTool==ToolInterchange){if(ImGui::IsMouseClicked(0))city.buildDiamondInterchange(*world,hover,status);}
                else if(cityTool==-1)selected=hover;
                else if(cityTool==ToolRoadRule)city.roadRule(*world,hover,uint8_t(ruleDirection|(signal?8:0)),status);
                else {Cell c=from;city.place(*world,c,BuildingKind(cityTool),status);while(c!=hover){if(c.x!=hover.x)c.x+=hover.x>c.x?1:-1;else c.z+=hover.z>c.z?1:-1;city.place(*world,c,BuildingKind(cityTool),status);}}
                unsaved=city.revision!=savedRevision;
            } else {size_t before=world->roadCount();world->stroke(from,hover,button==0);unsaved|=before!=world->roadCount();}
            previous=hover;previousButton=button;
        } else {previous={};previousButton=-1;}
        if((cityTool!=0&&cityTool!=ToolRail)||!app.active||io.MouseDown[1]||io.MouseDown[2]||ImGui::IsKeyPressed(ImGuiKey_Escape))roadStart={};
        if(config.cityMode&&(cityTool==0||cityTool==ToolRail)&&ImGui::IsMouseReleased(0)&&roadStart.x>=0){if(hover.x>=0){if(cityTool==ToolRail)city.buildRail(*world,roadStart,hover,false,status);else if(selectedRoad==RoadClass::Street)city.buildDiagonalRoad(*world,roadStart,hover,status);else city.buildRoad(*world,roadStart,hover,false,status,selectedRoad);}roadStart={};unsaved=city.revision!=savedRevision;}
        if(config.cityInput){
            if(frame==12&&(world->roadCount()!=startingRoadCount||city.treasury!=100000))throw std::runtime_error("Road input built before mouse release");
            if(frame==14&&(world->roadCount()!=startingRoadCount+7||world->road(262,257)||world->diagonalCount()!=0||city.treasury!=99860))throw std::runtime_error("Road input did not build only the final straight line");
            if(frame==26&&(!city.at({261,258})||city.at({261,258})->kind!=BuildingKind::Residential))throw std::runtime_error("City input test failed: zone selection / painting");
            if(frame==29&&city.at({261,258}))throw std::runtime_error("City input test failed: bulldozing");
            if(frame==56&&world->diagonalCount()!=0)throw std::runtime_error("Diagonal input built before mouse release");
            if(frame==58&&(world->diagonalCount()!=6||!world->canTravel({263,256},{264,257})||city.treasury!=97235))throw std::runtime_error("Diagonal input test failed: selection, drag, release or spending");
            if(frame==38&&(world->roadCount()!=startingRoadCount+7||!city.at({261,258})||city.at({261,258})->kind!=BuildingKind::Power||city.treasury!=97355))throw std::runtime_error("City input test failed: road stroke, UI capture, facility or spending");
        }
        if(config.interactionTest && (frame==18 || frame==25)) {
            if(world->roadCount()!=6 || world->road(262,259) || !world->road(259,259) || !world->road(265,259))
                throw std::runtime_error("Interaction test failed: mouse painting, erase, or UI capture.");
            status="Input checks passed: paint, erase, UI capture.";
        }
        if(config.edit && frame>=30) {
            int x=240+(frame%32),z=240+((frame/32)%32);
            world->setRoad(x,z,!world->road(x,z));
        }
        if(!config.cityMode) {
        ImGui::SetNextWindowPos({24,24},ImGuiCond_Always);ImGui::SetNextWindowSize({300,0});
        ImGui::Begin("Road tools",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove);
        ImGui::TextColored({0.43f,0.88f,0.77f,1},"V O X E L C I T Y");ImGui::TextDisabled("ROAD + TRAFFIC SIMULATION");ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        ImGui::Text("Two-lane road + sidewalk");ImGui::TextDisabled("2 x 2 grid spaces / 16 x 16 voxels");
        ImGui::TextWrapped("Paint connected roads. Corners and junctions form automatically.");ImGui::Spacing();
        ImGui::Text("Left drag");ImGui::SameLine(132);ImGui::TextDisabled("Build road");
        ImGui::Text("Right drag");ImGui::SameLine(132);ImGui::TextDisabled("Erase road");ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        if(ImGui::Button("Save map",{126,0})) try {if(auto path=mapDialog(window,true)) {world->save(*path);unsaved=false;status="Map saved.";}}catch(const std::exception& e){status=e.what();}
        ImGui::SameLine();if(ImGui::Button("Load map",{126,0})) {
            if(unsaved) ImGui::OpenPopup("Replace current map?");
            else try {if(auto path=mapDialog(window,false)) {world->load(*path);unsaved=false;status="Map loaded.";}}catch(const std::exception& e){status=e.what();}
        }
        if(ImGui::BeginPopupModal("Replace current map?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("The current map has unsaved changes.");
            if(ImGui::Button("Choose map")) {ImGui::CloseCurrentPopup();try {if(auto path=mapDialog(window,false)) {world->load(*path);unsaved=false;status="Map loaded.";}}catch(const std::exception& e){status=e.what();}}
            ImGui::SameLine();if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        ImGui::Checkbox("Show build grid",&grid);ImGui::Checkbox("Ray-traced sun shadows",&shadows);ImGui::Checkbox("VSync",&vsync);
        ImGui::Spacing();ImGui::Text("%zu road tiles%s",world->roadCount(),unsaved?"  *":"");ImGui::TextDisabled("1024 x 1024 grid / 512 x 512 roads");
        if(hover.x>=0) ImGui::TextDisabled("Grid origin  %d, %d",hover.x*RoadGridSpan,hover.z*RoadGridSpan);else ImGui::TextDisabled("Grid origin  --, --");
        ImGui::Spacing();ImGui::TextWrapped("%s",status.c_str());ImGui::End();
        } else {
            unsaved=city.revision!=savedRevision;
            ImGui::SetNextWindowPos({24,24},ImGuiCond_Always);ImGui::SetNextWindowSize({320,float(app.height)-160},ImGuiCond_Always);
            ImGui::Begin("Build your city",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
            ImGui::TextColored({0.43f,0.88f,0.77f,1},"V O X E L C I T Y");
            ImGui::TextWrapped("Connect to the regional highway or build passenger and cargo stations along the railway for immigration and trade. Right-click bulldozes ($5).");
            if(ImGui::Selectable("Inspect parcel",cityTool==-1))cityTool=-1;
            for(RoadClass road:{RoadClass::Street,RoadClass::OneWay,RoadClass::Avenue,RoadClass::Highway4,RoadClass::Highway6}){const auto& d=RoadDefinitions[size_t(road)];std::string label=std::string(d.name)+"  $"+std::to_string(d.cost)+"/step";if(ImGui::Selectable(label.c_str(),cityTool==0&&selectedRoad==road)){cityTool=0;selectedRoad=road;}if(road==RoadClass::Street){auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();roadButton={(a.x+b.x)*.5f,(a.y+b.y)*.5f};}}
            if(cityTool==0)ImGui::TextWrapped("Click and drag, then release. Wider roads reserve their complete corridor; highways do not provide zone frontage.");
            if(ImGui::Selectable("Roundabout prefab  6 x 6  $160",cityTool==ToolRoundabout))cityTool=ToolRoundabout;
            if(cityTool==ToolRoundabout)ImGui::TextWrapped("Click to place. Fixed entry / exit at each side midpoint; counterclockwise traffic. Right-click removes the whole piece ($40).");
            if(ImGui::Selectable("Diamond interchange  9 x 9  $2500",cityTool==ToolInterchange))cityTool=ToolInterchange;
            if(cityTool==ToolInterchange)ImGui::TextWrapped("Place on the median of a straight divided highway to add a fixed local-road crossing with highway access.");
            if(ImGui::Selectable("Road direction / signals",cityTool==ToolRoadRule))cityTool=ToolRoadRule;
            if(cityTool==ToolRoadRule){ImGui::Combo("Direction",&ruleDirection,"Two way\0North only\0East only\0South only\0West only\0");ImGui::Checkbox("Traffic signals",&signal);}
            if(ImGui::Selectable("Bulldoze  $5",cityTool==ToolBulldoze))cityTool=ToolBulldoze;
            if(ImGui::CollapsingHeader("Railway")){
                if(ImGui::Selectable("Double track  $40 / tile",cityTool==ToolRail))cityTool=ToolRail;
                for(auto kind:{BuildingKind::PassengerStation,BuildingKind::CargoTerminal,BuildingKind::TrainDepot}){auto& d=city.definition(kind);std::string label=d.name+"  $"+std::to_string(d.cost);if(ImGui::Selectable(label.c_str(),cityTool==int(kind)))cityTool=int(kind);}
                if(ImGui::Selectable("Highway rail overpass  $3000",cityTool==ToolRailOverpass))cityTool=ToolRailOverpass;
                if(ImGui::Button("Rotate station (R)"))railRotation=(railRotation+1)%4;
                if(ImGui::Button("Regional railway")){camera.focus={248.f*TileSize,0,RegionalRailRow*float(TileSize)};camera.distance=TileSize*40;}
                ImGui::TextWrapped("Drag tracks along one grid axis. Start on an existing track to connect. Stations need a local road and utilities. A depot enables local passenger trains.");
                ImGui::Text("Active trains: %zu | Passenger journeys: %llu",city.railway.trains().size(),city.railway.stats().passengers);
                ImGui::Text("Cargo moved: %llu units",city.railway.stats().cargo);
            }
            ImGui::SeparatorText("Zones and facilities");
            for(int k=1;k<int(BuildingKind::PassengerStation);++k){auto kind=BuildingKind(k);const auto& d=city.definition(kind);bool locked=!city.sandbox&&city.stats().milestone<d.unlock;
                ImGui::BeginDisabled(locked);std::string label=d.name+(!zone(kind)?"  $"+std::to_string(d.cost):"");if(ImGui::Selectable(label.c_str(),cityTool==k))cityTool=k;ImGui::EndDisabled();
                if(kind==BuildingKind::LowDensityResidential){auto p=ImGui::GetItemRectMin();residentialButton={p.x+20,p.y+10};};if(kind==BuildingKind::Power){auto p=ImGui::GetItemRectMin();powerButton={p.x+20,p.y+10};};
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Capacity %d | Upkeep $%d/min | Unlock %d residents",d.capacity,d.upkeep,d.unlock);}
            ImGui::Separator();
            if(ImGui::Button("Save city"))try{if(auto path=mapDialog(window,true)){city.save(*world,traffic,*path);savedRevision=city.revision;unsaved=false;status="City saved.";}}catch(const std::exception& e){status=e.what();}
            ImGui::SameLine();if(ImGui::Button("Load city"))ImGui::OpenPopup("Load another city?");
            if(ImGui::BeginPopupModal("Load another city?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::TextUnformatted("Unsaved progress will be replaced after a successful load.");
                if(ImGui::Button("Choose file")){ImGui::CloseCurrentPopup();try{if(auto path=mapDialog(window,false)){city.load(*world,traffic,*path);app.renderer->invalidateWorld();savedRevision=city.revision;unsaved=false;selected={};overlay=0;status="City loaded.";}}catch(const std::exception& e){status=e.what();}}
                ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
            if(ImGui::Button("New / starter city"))ImGui::OpenPopup("Start a new city?");
            if(ImGui::BeginPopupModal("Start a new city?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::TextUnformatted("This replaces the current city. Save first to keep it.");
                if(ImGui::Button("Starter layout")){city.scenario(*world,traffic,0);app.renderer->invalidateWorld();savedRevision=~uint64_t(0);camera.focus={248.f*TileSize,0,225.f*TileSize};camera.distance=TileSize*45;selected={};overlay=0;ImGui::CloseCurrentPopup();}
                if(ImGui::Button("New highway and railway map")){city=CitySimulation(config.trafficSeed);city.loadDefinitions(dataPath);city.newCity(*world,traffic);app.renderer->invalidateWorld();camera.focus={HighwayJunctionX*float(TileSize),0,(HighwayEastRow+6)*float(TileSize)};camera.distance=TileSize*45;savedRevision=~uint64_t(0);selected={};overlay=0;ImGui::CloseCurrentPopup();}
                ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
            if(ImGui::Checkbox("Unlimited-money sandbox",&city.sandbox))++city.revision;
            ImGui::Checkbox("Build grid",&grid);ImGui::Checkbox("Sun shadows",&shadows);ImGui::Checkbox("VSync",&vsync);
            ImGui::TextWrapped("%s",status.c_str());ImGui::End();
            ImGui::SetNextWindowPos({360,24},ImGuiCond_Always);ImGui::SetNextWindowSize({float(app.width)-384,0},ImGuiCond_Always);
            ImGui::Begin("City overview",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove);
            const auto& cs=city.stats();ImGui::Text("Residents %d / 10,000   Treasury $%.0f%s   Balance $%+.0f/min",cs.population,city.treasury,unsaved?" *":"",cs.balance);
            ImGui::Text("Happiness %.0f%%   Jobs %d   Employed %d   Milestone %d",cs.happiness,cs.jobs,cs.employed,cs.milestone);
            ImGui::Checkbox("Pause",&city.paused);ImGui::SameLine();ImGui::RadioButton("1x",&city.speed,1);ImGui::SameLine();ImGui::RadioButton("2x",&city.speed,2);ImGui::SameLine();ImGui::RadioButton("3x",&city.speed,3);
            ImGui::SameLine();if(ImGui::Button("Budget"))showBudget=!showBudget;ImGui::SameLine();ImGui::Checkbox("Guide",&guide);
            ImGui::SetNextItemWidth(200);if(ImGui::Combo("Overlay",&overlay,"Normal\0Road access\0Traffic pressure\0Electricity\0Water\0Sewage\0Waste collection\0Pollution\0Land value\0Healthcare coverage\0Fire coverage\0Police coverage\0School coverage\0Park coverage\0"))city.setOverlay(*world,CityOverlay(overlay));
            if(overlay)ImGui::TextDisabled("Red: shortage / poor condition   Yellow: strained   Green: healthy");
            if(world->highwayCount()){
                ImGui::Text("Westhaven <-> Eastbridge | Intercity trips %zu",city.regionalTrips());ImGui::SameLine();
                if(ImGui::Button("Highway junction")){camera.focus={HighwayJunctionX*float(TileSize),0,(HighwayEastRow+4)*float(TileSize)};camera.distance=TileSize*45;}
            }
            if(guide){const char* step=world->highwayCount()&&city.buildings().empty()?"1. Extend the local access road from the highway junction, then add utilities and zones.":world->roadCount()==0?"1. Paint roads to a map edge, or choose New / starter city.":cs.capacity[0]==0||cs.capacity[1]==0||cs.capacity[2]==0?"2. Place power, water and sewage facilities beside connected roads.":cs.population<500?"3. Paint green homes, blue shops and yellow industry beside roads. Balance RCI demand.":cs.population<2000?"4. Add garbage, healthcare, fire protection and parks. Inspect warnings.":cs.population>=10000?"10,000 residents reached. Keep services reliable and the city financially sustainable.":"5. Expand capacity, education and policing. Improve congested routes to reach 10,000.";ImGui::TextWrapped("%s",step);}
            ImGui::End();
            if(showBudget){ImGui::SetNextWindowSize({420,380},ImGuiCond_FirstUseEver);ImGui::Begin("Budget and demand",&showBudget);
                ImGui::Text("Income $%.0f / expenses $%.0f per minute",cs.income,cs.expenses);const char* names[]={"Residential tax","Commercial tax","Industrial tax"};
                for(int i=0;i<3;++i){if(ImGui::SliderInt(names[i],&city.taxes[i],0,30,"%d%%"))++city.revision;ImGui::ProgressBar(cs.demand[i],{-1,0},"Demand");}
                ImGui::Text("Rail infrastructure: $%.2f / minute",world->railUpkeep());
                ImGui::Text("Utilities: supply / demand");for(int i=0;i<3;++i)ImGui::Text("%s: %d / %d",i==0?"Power":i==1?"Water":"Sewage",cs.capacity[i],cs.consumption[i]);
                ImGui::BeginDisabled(city.loanTaken);if(ImGui::Button("Emergency loan: $25,000"))city.takeLoan();ImGui::EndDisabled();ImGui::TextWrapped("One loan per city; interest $250 per simulated minute. Taxes settle once per minute.");ImGui::End();}
            if(const auto* b=city.at(selected)){ImGui::SetNextWindowPos({float(app.width)-350,280},ImGuiCond_FirstUseEver);ImGui::SetNextWindowSize({326,0},ImGuiCond_Always);
                ImGui::Begin("Selected parcel",nullptr,ImGuiWindowFlags_AlwaysAutoResize);ImGui::Text("%s | Level %d",city.definition(b->kind).name.c_str(),b->level);
                if(b->kind==BuildingKind::CargoTerminal)ImGui::Text("Outbound cargo: %d / 120",city.waitingRailCargo(b->id));
                if(railFacility(b->kind))ImGui::Text("Regional service: %s | Arriving passengers: %d",city.railway.regional(b->id)?"Connected":"Disconnected",b->railPassengers);
                ImGui::Text("Residents %d | Workers %d | Goods %d",b->residents,b->workers,b->inventory);ImGui::Text("Happiness %.0f | Land value %.0f",b->happiness,b->landValue);ImGui::Text("Business productivity %.0f%%",b->productivity*100);
                ImGui::Text("Waste %.0f | Health %.0f | Fire %.0f",b->waste,b->health,b->fire);ImGui::Text("Crime %.0f | Education %.0f",b->crime,b->education);ImGui::Text("Coverage: clinic %s / fire %s",b->services[1]>0?"yes":"no",b->services[2]>0?"yes":"no");ImGui::Text("Police %s / school %s / park %s",b->services[3]>0?"yes":"no",b->services[4]>0?"yes":"no",b->services[5]>0?"yes":"no");
                ImGui::Text("Power %.0f%% | Water %.0f%% | Sewage %.0f%%",b->utilities[0]*100,b->utilities[1]*100,b->utilities[2]*100);
                ImGui::TextWrapped("%s",b->problem.empty()?"Operating normally":b->problem.c_str());if(ImGui::Button("Clear selection"))selected={};ImGui::End();}
            if(city.bankrupt)ImGui::OpenPopup("City insolvent");
            if(ImGui::BeginPopupModal("City insolvent",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::TextUnformatted("The treasury has been negative for three financial periods.");
                if(!city.loanTaken&&ImGui::Button("Take emergency loan")){city.takeLoan();city.paused=false;ImGui::CloseCurrentPopup();}
                if(ImGui::Button("Continue in sandbox")){city.sandbox=true;city.bankrupt=false;city.paused=false;++city.revision;ImGui::CloseCurrentPopup();}
                if(ImGui::Button("Return to paused city")){city.bankrupt=false;ImGui::CloseCurrentPopup();}ImGui::TextUnformatted("Use Save / Load or New city to recover or restart.");ImGui::EndPopup();}
        }
        ImGui::SetNextWindowPos({24,float(app.height)-110},ImGuiCond_Always);ImGui::SetNextWindowSize({300,0});
        ImGui::Begin("Camera",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove);
        ImGui::TextDisabled("Middle drag  Pan    |    Wheel  Zoom");ImGui::TextDisabled("Alt + middle drag  Orbit");
        if(ImGui::SmallButton("Home view")) camera=Camera{};ImGui::SameLine();if(ImGui::SmallButton("Whole map")) {camera=Camera{};camera.focus={WorldSize/2.f,0,WorldSize/2.f};camera.distance=WorldSize*1.86f;}ImGui::End();
        ImGui::SetNextWindowPos({float(app.width)-24,config.cityMode?float(app.height)-24:24},ImGuiCond_Always,{1,config.cityMode?1.f:0.f});
        ImGui::Begin("Performance",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove);
        ImGui::TextColored({0.43f,0.88f,0.77f,1},config.voxels?"VOXEL RAY TRACING":"LEGACY MESH RENDERER");
        ImGui::SetNextItemWidth(180);ImGui::Combo("Lighting quality",&config.quality,"Low\0Medium\0High\0");ImGui::Text("%.0f FPS   %.2f ms GPU",io.Framerate,app.renderer->stats.gpuMs);
        if(!config.cityMode){
        int population=int(traffic.target());
        ImGui::TextDisabled("Car target");ImGui::SetNextItemWidth(230);
        if(ImGui::InputInt("##Car target",&population,1000,10000))traffic.setTarget(size_t(std::clamp(population,0,100000)));
        ImGui::Checkbox("Pause traffic",&traffic.paused);
        }else {ImGui::Text("City %.2f ms | Travel %.1f sec",city.stats().tickMs,city.stats().averageTripSeconds);ImGui::Text("Deliveries %llu | Failed %llu",city.stats().deliveries,city.stats().failedTrips);}
        if(config.cityMode)ImGui::Text("Vehicles %zu",traffic.stats().active);else ImGui::Text("Cars  %zu / %zu",traffic.stats().active,traffic.target());
        ImGui::TextDisabled("Pending %zu | Completed %llu",traffic.stats().pending,traffic.stats().completed);
        ImGui::Checkbox("Diagnostics",&diagnostics);
        if(diagnostics) {
            const auto& s=app.renderer->stats;ImGui::TextDisabled("%s",app.renderer->adapterName.c_str());ImGui::Text("%u x %u",app.width,app.height);
            if(config.voxels){ImGui::Text("Voxel instances %u",s.voxelInstances);ImGui::Text("Scene %.2f | Surface %.2f ms",s.voxelSceneMs,s.voxelSurfaceMs);ImGui::Text("Lighting %.2f | Resolve %.2f ms",s.voxelLightMs,s.voxelResolveMs);}
            else {ImGui::Text("Chunks  %u / 1024",s.visibleChunks);ImGui::Text("Triangles  %llu / %llu",s.visibleTriangles,s.triangles);}
            ImGui::Text("Video memory  %.1f MiB",double(s.videoMemory)/(1024*1024));ImGui::Text("Rebuilt chunks  %u",s.rebuiltChunks);ImGui::Text("D3D12 errors  %u",s.debugErrors);
            ImGui::Text("Traffic %.2f ms | Routes %.2f ms",traffic.stats().tickMs,traffic.stats().routingMs);
            ImGui::Text("Render worker %.2f ms | Route latency %.1f ms",app.renderer->renderMs,traffic.stats().routeLatencyMs);
            ImGui::Text("Network jobs %zu | Replaced frames %llu",NetworkWorker::instance().pending(),app.renderer->dropped);
            ImGui::Text("Visible cars %u",s.visibleCars);
        }
        ImGui::End();
        if(app.closeRequested){if(!unsaved){quit=true;app.closeRequested=false;}else ImGui::OpenPopup("Save before exiting?");}
        if(ImGui::BeginPopupModal("Save before exiting?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted("This city has unsaved changes.");
            if(ImGui::Button("Save and exit"))try{if(auto path=mapDialog(window,true)){if(config.cityMode)city.save(*world,traffic,*path);else world->save(*path);quit=true;ImGui::CloseCurrentPopup();}}catch(const std::exception& e){status=e.what();}
            ImGui::SameLine();if(ImGui::Button("Discard and exit")){quit=true;ImGui::CloseCurrentPopup();}
            ImGui::SameLine();if(ImGui::Button("Keep playing")){app.closeRequested=false;ImGui::CloseCurrentPopup();}ImGui::EndPopup();}
        if(config.cityMode&&world->highwayCount()){
            auto highwayView=camera.view(app.width,app.height);
            auto clip=XMVector4Transform(XMVectorSet(HighwayJunctionX*float(TileSize)+8,3,(HighwayWestRow-1)*float(TileSize),1),XMLoadFloat4x4(&highwayView.viewProjection));
            float cw=XMVectorGetW(clip);if(cw>0){float x=(XMVectorGetX(clip)/cw+1)*app.width*.5f,y=(1-XMVectorGetY(clip)/cw)*app.height*.5f;
                const char* label="< WESTHAVEN    REGIONAL HIGHWAY    EASTBRIDGE >";auto size=ImGui::CalcTextSize(label);x-=size.x*.5f;
                if(x>=350&&x+size.x<app.width-20&&y>240&&y+size.y<app.height-200){auto* draw=ImGui::GetBackgroundDrawList();draw->AddRectFilled({x-10,y-6},{x+size.x+10,y+size.y+6},IM_COL32(18,65,46,240),4);draw->AddText({x,y},IM_COL32(235,245,230,255),label);}
            }
        }
        if(config.cameraPath)camera.yaw=(config.captureCamera?config.captureCamera->yaw:.65f)+float(frame)*.001f;
        if(config.cityMode&&cityTool==0&&roadStart.x>=0&&hover.x>=0){
            auto vp=camera.view(app.width,app.height).viewProjection;auto line=World::diagonalLine(roadStart,hover);
            auto project=[&](Cell c){auto p=XMVector3TransformCoord(XMVectorSet(c.x*16.f+8,1,c.z*16.f+8,1),XMLoadFloat4x4(&vp));return ImVec2{(XMVectorGetX(p)+1)*app.width*.5f,(1-XMVectorGetY(p))*app.height*.5f};};
            int tx=(hover.x>roadStart.x)-(hover.x<roadStart.x),tz=(hover.z>roadStart.z)-(hover.z<roadStart.z);if(!tx&&!tz)tx=1;Cell perpendicular{-tz,tx};unsigned width=RoadDefinitions[size_t(selectedRoad)].footprint;int firstOffset=width==2?0:-int(width/2),lastOffset=int(width/2);
            for(int offset=firstOffset;offset<=lastOffset;++offset)for(size_t i=1;i<line.size();++i){Cell a{line[i-1].x+perpendicular.x*offset,line[i-1].z+perpendicular.z*offset},b{line[i].x+perpendicular.x*offset,line[i].z+perpendicular.z*offset};ImGui::GetForegroundDrawList()->AddLine(project(a),project(b),offset==0?IM_COL32(80,220,205,220):IM_COL32(80,180,205,150),6.f);}
        }
        if(config.cityMode&&hover.x>=0&&(cityTool==ToolRail||cityTool==ToolRailOverpass||(cityTool>0&&cityTool<int(BuildingKind::Count)&&railFacility(BuildingKind(cityTool))))){
            static Cell oldHover{},oldStart{};static int oldTool=-99,oldRotation=-1;static uint64_t oldRevision=~uint64_t(0);static double oldMoney=-1;static bool good=false;static double price=0;static std::string explanation;static std::vector<Cell> footprint;
            if(hover!=oldHover||roadStart!=oldStart||cityTool!=oldTool||railRotation!=oldRotation||city.revision!=oldRevision||city.treasury!=oldMoney){
                oldHover=hover;oldStart=roadStart;oldTool=cityTool;oldRotation=railRotation;oldRevision=city.revision;oldMoney=city.treasury;
                auto preview=std::make_unique<World>(*world);CitySimulation candidate=city;candidate.sandbox=false;candidate.treasury=1e12;footprint.clear();
                if(cityTool==ToolRail){Cell railStart=roadStart.x>=0?roadStart:hover;good=candidate.buildRail(*preview,railStart,hover,false,explanation);if(railStart.x==hover.x||railStart.z==hover.z)footprint=World::diagonalLine(railStart,hover);else footprint={hover};}
                else if(cityTool==ToolRailOverpass){good=candidate.buildRailOverpass(*preview,hover,explanation);if(good){int owner=hover.z*MapSize+hover.x;for(int i=0;i<MapSize*MapSize;++i)if(preview->rails()[i].bridge==owner)footprint.push_back({i%MapSize,i/MapSize});}else footprint={hover};}
                else {Building b;b.cell=hover;b.kind=BuildingKind(cityTool);b.variant=uint8_t(railRotation);footprint=CitySimulation::occupiedFootprint(b);good=candidate.placeRailFacility(*preview,hover,b.kind,railRotation,explanation);}
                price=1e12-candidate.treasury;if(good&&!city.sandbox&&city.treasury<price){good=false;explanation="Insufficient funds.";}
            }
            auto vp=camera.view(app.width,app.height).viewProjection;auto project=[&](float x,float z){auto p=XMVector3TransformCoord(XMVectorSet(x,1,z,1),XMLoadFloat4x4(&vp));return ImVec2{(XMVectorGetX(p)+1)*app.width*.5f,(1-XMVectorGetY(p))*app.height*.5f};};
            auto* draw=ImGui::GetForegroundDrawList();for(auto c:footprint)draw->AddQuad(project(c.x*16.f,c.z*16.f),project((c.x+1)*16.f,c.z*16.f),project((c.x+1)*16.f,(c.z+1)*16.f),project(c.x*16.f,(c.z+1)*16.f),good?IM_COL32(70,230,210,230):IM_COL32(255,90,70,230),2);
            if(!io.WantCaptureMouse){ImGui::BeginTooltip();ImGui::Text("Rail construction: $%.0f%s",price,city.sandbox?" (sandbox)":"");ImGui::TextUnformatted(explanation.c_str());ImGui::EndTooltip();}
        }
        if(ImGui::IsKeyPressed(ImGuiKey_R)&&!io.WantTextInput)railRotation=(railRotation+1)%4;
        ImGui::Render();
        View view=camera.view(app.width,app.height);view.showUI=!config.noUI;view.voxels=config.voxels;view.quality=config.quality;view.hover=hover;view.signalPhase=float((city.ticks()/180)%2);view.erase=button==1||(config.cityMode&&cityTool>0&&cityTool<int(BuildingKind::Count)&&(world->road(hover.x,hover.z)||city.at(hover)));view.grid=grid;view.shadows=shadows;
        if(config.cityMode&&cityTool==ToolRoundabout&&button!=1&&hover.x>=0){view.hoverSpan=3;view.erase=!World::valid(hover.x+2,hover.z+2)||(!city.sandbox&&city.treasury<160);for(int z=0;z<3;++z)for(int x=0;x<3;++x){Cell c{hover.x+x,hover.z+z};view.erase|=world->roadOccupies(c)||city.at(c)||world->roundaboutOrigin(c).x>=0;}}
        if(config.cityMode&&cityTool==ToolInterchange&&button!=1&&hover.x>=0){view.hover={hover.x-4,hover.z-4};view.hoverSpan=9;view.erase=!World::valid(hover.x-4,hover.z-4)||!World::valid(hover.x+4,hover.z+4)||world->roadClass(hover)!=RoadClass::Median||(!city.sandbox&&city.treasury<2500);}
        if(config.cityMode&&(button==1||cityTool==ToolBulldoze)&&world->roundaboutOrigin(hover).x>=0){view.hover=world->roundaboutOrigin(hover);view.hoverSpan=3;view.erase=true;}
        bool final=config.frames>0 && frame+1>=config.frames;
        if(config.cityMode){if(config.cityStress)city.replenishTrafficStress(traffic,size_t(config.cityStress));if(config.trafficBenchmark&&!city.paused){for(int i=0;i<city.speed;++i)city.tick(*world,traffic);}else city.update(*world,traffic,elapsed/1000);
            if(!city.paused&&!autosaveFolder.empty()){autosaveElapsed+=elapsed/1000;if(autosaveElapsed>=300){autosaveElapsed=0;try{std::filesystem::create_directories(autosaveFolder);city.save(*world,traffic,autosaveFolder/("autosave-"+std::to_string(autosaveSlot++%3)+".vcity"));status="Autosaved to "+autosaveFolder.string();}catch(const std::exception& e){status=e.what();}}}
        }else if(config.trafficBenchmark && !traffic.paused)traffic.tick(*world);else traffic.update(*world,elapsed/1000);
        if(frame>=30){trafficTimes.push_back(traffic.stats().tickMs);routingTimes.push_back(traffic.stats().routingMs);minimumCars=std::min(minimumCars,traffic.stats().active);maximumCars=std::max(maximumCars,traffic.stats().active);}
        auto capture=final?config.capture:std::filesystem::path{};
        if(!config.captureSequence.empty()&&frame>=30){std::filesystem::create_directories(config.captureSequence);std::ostringstream name;name<<"frame-"<<std::setw(5)<<std::setfill('0')<<frame-30<<".bmp";capture=config.captureSequence/name.str();}
        auto cars=traffic.instances();std::vector<CarInstance> vehicles(cars.begin(),cars.end());
        if(config.cityMode){auto trains=city.railway.instances(*world);vehicles.insert(vehicles.end(),trains.begin(),trains.end());}
        app.renderer->render(*world,view,vsync,capture,vehicles);
        if(frame>=30&&capture.empty())mainWorkTimes.push_back(std::chrono::duration<double,std::milli>(Clock::now()-now).count());
        if(config.frames>0&&!config.threadingTest)app.renderer->waitIdle();
        if(config.windowTest && frame==35) SetWindowPos(window,nullptr,0,0,1280,800,SWP_NOMOVE|SWP_NOZORDER);
        if(config.windowTest && frame==45) {ShowWindow(window,SW_MINIMIZE);ShowWindow(window,SW_RESTORE);}
        ++frame;if(final) quit=true;
    }
    app.renderer->waitIdle();
    if(config.threadingTest&&(!app.renderer->dropped||app.renderer->presented>=uint64_t(frame)))throw std::runtime_error("Main thread did not advance independently of delayed renderer");
    if(!config.report.empty()) {
        auto average=[](const std::vector<double>& v) {double total=0;for(auto x:v)total+=x;return v.empty()?0:total/double(v.size());};
        auto sorted=cpuTimes;std::sort(sorted.begin(),sorted.end());double p95=sorted.empty()?0:sorted[size_t((sorted.size()-1)*0.95)];
        auto trafficSorted=trafficTimes;std::sort(trafficSorted.begin(),trafficSorted.end());
        auto percentile=[&](double p){return trafficSorted.empty()?0:trafficSorted[size_t((trafficSorted.size()-1)*p)];};
        std::ofstream out(config.report);out<<std::fixed<<std::setprecision(3)
            <<"{\n  \"adapter\": \""<<app.renderer->adapterName<<"\",\n  \"width\": "<<app.width<<", \"height\": "<<app.height
            <<",\n  \"frames\": "<<frame<<", \"road_tiles\": "<<world->roadCount()<<",\n  \"average_frame_ms\": "<<average(cpuTimes)<<", \"p95_frame_ms\": "<<p95<<", \"max_frame_ms\": "<<maxFrame
            <<",\n  \"average_gpu_ms\": "<<average(gpuTimes)<<", \"video_memory_mib\": "<<double(app.renderer->stats.videoMemory)/(1024*1024)
            <<",\n  \"average_voxel_scene_ms\": "<<average(sceneTimes)
            <<",\n  \"renderer\": \""<<(config.voxels?"voxel":"legacy")<<"\", \"quality\": "<<config.quality<<", \"voxel_instances\": "<<app.renderer->stats.voxelInstances
            <<",\n  \"voxel_memory_mib\": "<<double(app.renderer->stats.voxelBytes)/(1024*1024)<<", \"peak_video_memory_mib\": "<<double(app.renderer->stats.peakVideoMemory)/(1024*1024)
            <<",\n  \"average_voxel_surface_ms\": "<<average(surfaceTimes)<<", \"average_voxel_lighting_ms\": "<<average(lightTimes)<<", \"average_voxel_resolve_ms\": "<<average(resolveTimes)
            <<",\n  \"voxel_cpu_retire_ms\": "<<app.renderer->stats.voxelCpuRetireMs<<", \"voxel_cpu_terrain_ms\": "<<app.renderer->stats.voxelCpuTerrainMs<<", \"voxel_cpu_pack_ms\": "<<app.renderer->stats.voxelCpuPackMs<<", \"voxel_cpu_instances_ms\": "<<app.renderer->stats.voxelCpuInstancesMs
            <<",\n  \"triangles\": "<<app.renderer->stats.triangles<<", \"visible_chunks\": "<<app.renderer->stats.visibleChunks
            <<",\n  \"cars\": "<<traffic.stats().active<<", \"car_target\": "<<traffic.target()<<", \"min_cars\": "<<(minimumCars==SIZE_MAX?0:minimumCars)<<", \"max_cars\": "<<maximumCars
            <<",\n  \"traffic_p50_ms\": "<<percentile(.5)<<", \"traffic_p95_ms\": "<<percentile(.95)<<", \"routing_average_ms\": "<<average(routingTimes)
            <<",\n  \"traffic_pending\": "<<traffic.stats().pending<<", \"completed_trips\": "<<traffic.stats().completed<<", \"traffic_memory_mib\": "<<double(traffic.stats().memoryBytes)/(1024*1024)
            <<",\n  \"rail_trains\": "<<city.railway.trains().size()<<", \"rail_passengers\": "<<city.railway.stats().passengers<<", \"rail_cargo\": "<<city.railway.stats().cargo
            <<",\n  \"city_population\": "<<city.stats().population<<", \"city_tick_ms\": "<<city.stats().tickMs<<", \"city_balance\": "<<city.stats().balance
            <<",\n  \"traffic_seed\": "<<config.trafficSeed<<", \"traffic_warmup_seconds\": "<<trafficWarmupSeconds
            <<",\n  \"debug_errors\": "<<app.renderer->stats.debugErrors<<", \"total_seconds\": "<<std::chrono::duration<double>(Clock::now()-start).count()<<",\n  \"rendered_frames\": "<<app.renderer->presented<<", \"replaced_frames\": "<<app.renderer->dropped
            <<",\n  \"presentation_average_ms\": "<<app.renderer->frameAverageMs<<", \"presentation_p95_ms\": "<<app.renderer->frameP95Ms
            <<",\n  \"main_thread_work_average_ms\": "<<average(mainWorkTimes)
            <<",\n  \"render_worker_average_ms\": "<<app.renderer->renderAverageMs<<", \"snapshot_ms\": "<<app.renderer->snapshotMs
            <<",\n  \"route_latency_ms\": "<<traffic.stats().routeLatencyMs<<", \"stale_routes\": "<<traffic.stats().staleRoutes<<", \"network_queue\": "<<NetworkWorker::instance().pending()<<"\n}\n";
        if(!out) throw std::runtime_error("Could not write benchmark report.");
    }
    int result=app.renderer->stats.debugErrors?2:0;
    app.renderer.reset();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext();DestroyWindow(window);
    return result;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    try {return vc::run(instance);}
    catch(const std::exception& e) {
        std::ofstream log("voxelcity-error.log",std::ios::app);log<<e.what()<<'\n';log.flush();
        // Automated runs must return an error rather than block on a modal dialog.
        if(wcsstr(GetCommandLineW(),L"--threading-test")==nullptr && wcsstr(GetCommandLineW(),L"--city-input-test")==nullptr && wcsstr(GetCommandLineW(),L"--frames")==nullptr && wcsstr(GetCommandLineW(),L"--smoke-test")==nullptr && wcsstr(GetCommandLineW(),L"--interaction-test")==nullptr)
            MessageBoxA(nullptr,e.what(),"VoxelCity startup / rendering error",MB_OK|MB_ICONERROR);
        return 1;
    }
}
