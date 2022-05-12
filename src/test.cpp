#include "os/os.h"
#include "hsd/cobj.h"
#include "hsd/gobj.h"
#include "hsd/video.h"
#include "melee/menu.h"
#include "util/hooks.h"
#include "util/texture_swap.h"
#include "util/draw/render.h"
#include "imgui.h"
#include "imgui/backends/imgui_impl_gc.h"
#include "imgui/backends/imgui_impl_gx.h"

extern "C" void CSS_Setup();
extern "C" void HSD_ResetScene();

static char text_buf[256] = { 't', 'e', 's', 't', '\0' };

static void draw(HSD_GObj *gobj, u32 pass)
{
	if (pass != HSD_RP_BOTTOMHALF)
		return;

	ImGui_ImplGC_NewFrame();
	ImGui_ImplGX_NewFrame();
	ImGui::NewFrame();

	ImGui::SetNextWindowPos({0, 0});
	ImGui::Begin("super smash brothers melee");
	ImGui::Text("The year is 20XX. Everyone plays Fox at TAS levels of\n"
	            "perfection. Because of this, the winner of a match depends\n"
	            "solely on port priority. The Rock Paper Scissors metagame\n"
	            "has evolved to ridiculous levels due to it being the only\n"
	            "remaining factor to decide matches. Humanity has reached its\n"
	            "pinnacle. The low tier peasants are living in poverty. It\n"
	            "seems nothing can stop the great leader of 20XX, Aziz \"Hax\"\n"
	            "Al-Yami, and his army, the Fox monks who live in great\n"
	            "monasteries where they levitate while TASing Fox with one\n"
	            "hand, and winning tournaments with the other. The tournament\n"
	            "metagame has gotten to this point where everything is played\n"
	            "out to theoretical perfection, so tournament goers play Rock\n"
	            "Paper Scissors for port priority, and that's the game.\n"
	            "The leaders of the anti-20XX movement aim to keep 20XX from\n"
	            "coming. These warriors include Juan \"Hungrybox\" DeBiedma,\n"
	            "Kevin \"PewPewU\" Toy, Kevin \"PPMD\" Nanney, and Jeffery \"Axe\"\n"
	            "Williamson. They are all fighting to keep the apocalypse at\n"
	            "bay, the Fox apocalypse. But their efforts are futile. Their\n"
	            "silly Marths, Pikachus, Falcos, and Jigglypuffs are no match\n"
	            "for Fox, the only viable character in Super Smash Bros.\n"
	            "Melee for the Nintendo GameCube. Try as you will, but 20XX\n"
	            "is coming. Or maybe, it's already here.");
	if (ImGui::Button("gaming"))
		OSReport("gaming");
	if (ImGui::Button("fox mccloud"))
		OSReport("fox mccloud");

	ImGui::InputTextMultiline("input field", text_buf, IM_ARRAYSIZE(text_buf));

	ImGui::End();

	ImGui::Render();
        ImGui_ImplGX_RenderDrawData(ImGui::GetDrawData());
}

HOOK(CSS_Setup, [&]()
{
	original();

	auto *gobj = GObj_Create(4, 5, 0x80);
	GObj_SetupGXLink(gobj, draw, 1, 0x80);
});

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
});