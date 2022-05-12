// dear imgui: Platform Backend for Nintendo GameCube
// This needs to be used along with the GX Renderer.

#pragma once

#include "imgui.h"      // IMGUI_IMPL_API

IMGUI_IMPL_API bool ImGui_ImplGC_Init();
IMGUI_IMPL_API void ImGui_ImplGC_Shutdown();
IMGUI_IMPL_API void ImGui_ImplGC_NewFrame();