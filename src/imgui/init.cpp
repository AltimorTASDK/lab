#include "melee/scene.h"
#include "imgui/events.h"
#include "imgui/backends/imgui_impl_gc.h"
#include "imgui/backends/imgui_impl_gx.h"
#include "util/hooks.h"
#include <imgui.h>

extern "C" void HSD_ResetScene();

HOOK(HSD_ResetScene, [&]()
{
	if (ImGui::GetCurrentContext() != nullptr) {
		ImGui_ImplGX_Shutdown();
		ImGui_ImplGC_Shutdown();
		ImGui::DestroyContext();
	}

	original();

	if (SceneMajor != Scene_VsMode && SceneMajor != Scene_Training)
		return;

	if (SceneMinor != VsScene_Game)
		return;

	ImGui::CreateContext();
	ImGui_ImplGC_Init();
	ImGui_ImplGX_Init();

	auto &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	events::imgui::init.fire();
});