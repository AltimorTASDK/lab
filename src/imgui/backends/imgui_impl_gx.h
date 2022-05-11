// dear imgui: Renderer Backend for GameCube GX
// This needs to be used along with the GC Backend

#pragma once

#include "imgui.h"      // IMGUI_IMPL_API

IMGUI_IMPL_API bool ImGui_ImplGX_Init();
IMGUI_IMPL_API void ImGui_ImplGX_Shutdown();
IMGUI_IMPL_API void ImGui_ImplGX_NewFrame();
IMGUI_IMPL_API void ImGui_ImplGX_RenderDrawData(ImDrawData *draw_data);

// Called by Init/NewFrame
IMGUI_IMPL_API bool ImGui_ImplGX_CreateFontsTexture();
IMGUI_IMPL_API bool ImGui_ImplGX_CreateDeviceObjects();
