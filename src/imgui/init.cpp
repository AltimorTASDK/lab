#include "imgui/events.h"
#include "util/hooks.h"
#include "imgui.h"
#include "imgui/backends/imgui_impl_gc.h"
#include "imgui/backends/imgui_impl_gx.h"

#include "resources/fonts/cascadia_mono.ttf.h"

extern "C" void HSD_ResetScene();

HOOK(HSD_ResetScene, [&]()
{
	if (ImGui::GetCurrentContext() != nullptr) {
		ImGui_ImplGX_Shutdown();
		ImGui_ImplGC_Shutdown();
		ImGui::DestroyContext();
	}

	original();

	ImGui::CreateContext();
	ImGui_ImplGC_Init();
	ImGui_ImplGX_Init();

	auto &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	ImFontConfig font_config;
	font_config.FontDataOwnedByAtlas = false;

	auto *data = cascadia_mono_ttf_data;
	const auto size = sizeof(cascadia_mono_ttf_data);
	io.FontDefault = io.Fonts->AddFontFromMemoryTTF(data, size, 16.f, &font_config);
});