#include "os/os.h"
#include "hsd/gobj.h"
#include "melee/menu.h"
#include "util/hooks.h"
#include "imgui/backends/imgui_impl_gc.h"
#include "imgui/backends/imgui_impl_gx.h"

extern "C" void Scene_Main();
extern "C" void CSS_Init(void *enter_data);

static void draw(HSD_GObj *gobj, u32 pass)
{
	OSReport("ImplGX_NewFrame\n");
	ImGui_ImplGX_NewFrame();
	OSReport("ImGui::NewFrame\n");
	ImGui::NewFrame();

	OSReport("ImGui::Begin\n");
	ImGui::Begin("Hello, world!");
	OSReport("ImGui::Text\n");
	ImGui::Text("This is some useful text.");
	OSReport("ImGui::End\n");
	ImGui::End();

	OSReport("ImGui::Render\n");
	ImGui::Render();
	OSReport("ImGui::ImplGX_RenderDrawData\n");
        ImGui_ImplGX_RenderDrawData(ImGui::GetDrawData());
}

HOOK(CSS_Init, [&](void *enter_data)
{
	original(enter_data);

	OSReport("CreateContext\n");
	ImGui::CreateContext();
	OSReport("ImplGC_Init\n");
	ImGui_ImplGC_Init();
	OSReport("ImplGX_Init\n");
	ImGui_ImplGX_Init();
	OSReport("GObj_Create\n");

	auto *gobj = GObj_Create(GOBJ_CLASS_PROC, GOBJ_PLINK_PROC, 0x80);
	Menu_SetGObjPrio(gobj);
	GObj_SetupGXLink(gobj, draw, GOBJ_GXLINK_MENU_TOP, 0x80);
});

HOOK(Scene_Main, [&]
{
	OSReport("gaming\n");
	original();
});