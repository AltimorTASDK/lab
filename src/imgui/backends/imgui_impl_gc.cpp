// dear imgui: Platform Backend for Nintendo GameCube
// This needs to be used along with the GX Renderer.

#include "imgui.h"
#include "imgui_impl_gc.h"

// GC Data
struct ImGui_ImplGC_Data {
};

// Backend data stored in io.BackendPlatformUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
// FIXME: multi-context support is not well tested and probably dysfunctional in this backend.
// FIXME: some shared resources (mouse cursor shape, gamepad) are mishandled when using multi-context.
static ImGui_ImplGC_Data *ImGui_ImplGC_GetBackendData()
{
	return ImGui::GetCurrentContext() != nullptr
		? (ImGui_ImplGC_Data*)ImGui::GetIO().BackendPlatformUserData
		: NULL;
}

bool ImGui_ImplGC_Init()
{
	auto &io = ImGui::GetIO();
	IM_ASSERT(io.BackendPlatformUserData == NULL && "Already initialized a platform backend!");

	// Setup backend capabilities flags
	auto *bd = IM_NEW(ImGui_ImplGC_Data)();
	io.BackendPlatformUserData = (void*)bd;
	io.BackendPlatformName = "imgui_impl_gc";

	// Setup display size
	io.DisplaySize = ImVec2(640.f, 480.f);
	io.DisplayFramebufferScale = ImVec2(1.f, 1.f);

	// Setup time step
	io.DeltaTime = 1.f / 60.f;

	return true;
}

void ImGui_ImplGC_Shutdown()
{
	auto *bd = ImGui_ImplGC_GetBackendData();
	IM_ASSERT(bd != NULL && "No platform backend to shutdown, or already shutdown?");
	IM_DELETE(bd);

	auto &io = ImGui::GetIO();
	io.BackendPlatformName = NULL;
	io.BackendPlatformUserData = NULL;
}
