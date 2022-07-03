#include "imgui/events.h"
#include "imgui/backends/imgui_impl_gc.h"
#include "imgui/backends/imgui_impl_gx.h"
#include "util/hooks.h"
#include <imgui.h>

extern "C" void GObj_RenderAll();

HOOK(GObj_RenderAll, [&]()
{
	original();

	if (ImGui::GetCurrentContext() == nullptr)
		return;

	ImGui_ImplGC_NewFrame();
	ImGui_ImplGX_NewFrame();
	ImGui::NewFrame();

	events::imgui::draw.fire();

	ImGui::Render();
        ImGui_ImplGX_RenderDrawData(ImGui::GetDrawData());
});