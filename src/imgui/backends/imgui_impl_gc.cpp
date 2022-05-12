// dear imgui: Platform Backend for Nintendo GameCube
// This needs to be used along with the GX Renderer.

#include "imgui.h"
#include "imgui_impl_gc.h"
#include "hsd/pad.h"
#include "util/vector.h"

struct ImGui_ImplGC_Pad {
	u32 buttons;
	u32 instant_buttons;
	u32 released_buttons;
	vec2 stick;
	vec2 cstick;
	f32 analog_l;
	f32 analog_r;
};

// GC Data
struct ImGui_ImplGC_Data {
	ImGui_ImplGC_Pad last_pad;
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
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

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

// Aggregate pad state from all ports
static ImGui_ImplGC_Pad ImGui_ImplGC_AggregatePadStatus()
{
	ImGui_ImplGC_Pad all;

	for (auto i = 0; i < 4; i++) {
		const auto &pad = HSD_PadMasterStatus[i];
		if (pad.err != 0)
			continue;

		all.buttons |= pad.buttons;

		if (std::abs(pad.stick.x) > .2750f)
			all.stick.x += pad.stick.x;
		if (std::abs(pad.stick.y) > .2750f)
			all.stick.y += pad.stick.y;
		if (std::abs(pad.cstick.x) > .2750f)
			all.cstick.x += pad.cstick.x;
		if (std::abs(pad.cstick.y) > .2750f)
			all.cstick.y += pad.cstick.y;
		if (pad.analog_l > .3000f)
			all.analog_l += pad.analog_l;
		if (pad.analog_r > .3000f)
			all.analog_r += pad.analog_r;
	}

	const auto *bd = ImGui_ImplGC_GetBackendData();
	all.instant_buttons  = (all.buttons ^ bd->last_pad.buttons) & all.buttons;
	all.released_buttons = (all.buttons ^ bd->last_pad.buttons) & ~all.buttons;

	return all;
}

static void ImGui_ImplGC_CheckButton(const ImGui_ImplGC_Pad &pad, int button, int imgui_key)
{
	auto &io = ImGui::GetIO();

	if (pad.instant_buttons & button)
		io.AddKeyEvent(imgui_key, true);
	else if (pad.released_buttons & button)
		io.AddKeyEvent(imgui_key, false);
}

static void ImGui_ImplGC_CheckAnalog(float value, float last_value, int up_key, int down_key)
{
	auto &io = ImGui::GetIO();

	if (value == last_value)
		return;

	if (value > 0.f) {
		io.AddKeyAnalogEvent(up_key, true, std::min(value, 1.f));
		if (last_value < 0.f)
			io.AddKeyAnalogEvent(down_key, false, 0.f);
	} else if (value < 0.f) {
		io.AddKeyAnalogEvent(down_key, true, std::min(-value, 1.f));
		if (last_value > 0.f)
			io.AddKeyAnalogEvent(up_key, false, 0.f);
	} else {
		io.AddKeyAnalogEvent(up_key, false, 0.f);
		io.AddKeyAnalogEvent(down_key, false, 0.f);
	}
}

void ImGui_ImplGC_NewFrame()
{
	auto *bd = ImGui_ImplGC_GetBackendData();
	IM_ASSERT(bd != NULL && "Did you call ImGui_ImplSDL2_Init()?");
	auto &io = ImGui::GetIO();

	const auto pad = ImGui_ImplGC_AggregatePadStatus();
	const auto &last_pad = bd->last_pad;

	ImGui_ImplGC_CheckButton(pad, Button_Start,     ImGuiKey_GamepadStart);
	ImGui_ImplGC_CheckButton(pad, Button_Y,         ImGuiKey_GamepadFaceUp);
	ImGui_ImplGC_CheckButton(pad, Button_A,         ImGuiKey_GamepadFaceDown);
	ImGui_ImplGC_CheckButton(pad, Button_X,         ImGuiKey_GamepadFaceLeft);
	ImGui_ImplGC_CheckButton(pad, Button_B,         ImGuiKey_GamepadFaceRight);
	ImGui_ImplGC_CheckButton(pad, Button_Z,         ImGuiKey_GamepadR1);

	if (((pad.buttons ^ last_pad.buttons) & Button_L) || pad.analog_l != last_pad.analog_l)
		io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, pad.buttons & Button_L, pad.analog_l);
	if (((pad.buttons ^ last_pad.buttons) & Button_R) || pad.analog_r != last_pad.analog_r)
		io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, pad.buttons & Button_R, pad.analog_r);

	ImGui_ImplGC_CheckAnalog(pad.stick.x,  last_pad.stick.x,  ImGuiKey_GamepadLStickRight,
	                                                          ImGuiKey_GamepadLStickLeft);
	ImGui_ImplGC_CheckAnalog(pad.stick.y,  last_pad.stick.y,  ImGuiKey_GamepadLStickUp,
	                                                          ImGuiKey_GamepadLStickDown);
	ImGui_ImplGC_CheckAnalog(pad.cstick.x, last_pad.cstick.x, ImGuiKey_GamepadRStickRight,
	                                                          ImGuiKey_GamepadRStickLeft);
	ImGui_ImplGC_CheckAnalog(pad.cstick.y, last_pad.cstick.y, ImGuiKey_GamepadRStickUp,
	                                                          ImGuiKey_GamepadRStickDown);

	// Also register stick inputs as dpad inputs for imgui nav
	ImGui_ImplGC_CheckAnalog(pad.stick.x,  last_pad.stick.x,  ImGuiKey_GamepadDpadRight,
	                                                          ImGuiKey_GamepadDpadLeft);
	ImGui_ImplGC_CheckAnalog(pad.stick.y,  last_pad.stick.y,  ImGuiKey_GamepadDpadUp,
	                                                          ImGuiKey_GamepadDpadDown);

	bd->last_pad = pad;
}