#include "VoxelRenderer.h"
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
int main()try{
    ComPtr<ID3D12Debug> debug;if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;if(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory))))throw std::runtime_error("No DXGI factory");
    ComPtr<ID3D12Device5> device;
    for(unsigned i=0;;++i){ComPtr<IDXGIAdapter1> adapter;if(factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter))==DXGI_ERROR_NOT_FOUND)break;
        if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device))))break;}
    if(!device)throw std::runtime_error("GPU conformance tests require the supported DXR GPU");
    {vc::VoxelRenderer renderer(device.Get());renderer.validateTraversal();}
    ComPtr<ID3D12InfoQueue> info;if(SUCCEEDED(device.As(&info)))for(uint64_t i=0;i<info->GetNumStoredMessages();++i){SIZE_T bytes=0;info->GetMessage(i,nullptr,&bytes);std::vector<char> data(bytes);auto* m=reinterpret_cast<D3D12_MESSAGE*>(data.data());info->GetMessage(i,m,&bytes);if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR)throw std::runtime_error(m->pDescription);}
    std::cout<<"8,192 GPU voxel rays match CPU traversal; zero DirectX errors.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
