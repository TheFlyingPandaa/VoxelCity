#pragma once
#include <imgui.h>
#include <vector>
struct ID3D12GraphicsCommandList;
struct UiDrawList {
    std::vector<ImDrawVert> VtxBuffer;
    std::vector<ImDrawIdx> IdxBuffer;
    std::vector<ImDrawCmd> CmdBuffer;
};
struct UiDrawData {
    int TotalVtxCount=0,TotalIdxCount=0,CmdListsCount=0;
    ImVec2 DisplayPos{},DisplaySize{},FramebufferScale{1,1};
    std::vector<UiDrawList> CmdLists;
};
void ConfigureRenderUI(ImFontAtlas* fonts);
void ImGui_ImplDX12_RenderDrawData(UiDrawData*,ID3D12GraphicsCommandList*);
