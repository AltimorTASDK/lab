// dear imgui: Platform Backend for Nintendo GameCube
// This needs to be used along with the GX Renderer.

#include "imgui.h"
#include "imgui_impl_gc.h"
#include "os/serial.h"
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
	ImGui_ImplGC_Pad last_pad[4];
	SIKeyboard keyboard[4];
	SIKeyboard last_keyboard[4];
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

static void ImGui_ImplGC_CheckKey(const SIKeyboard &kb, const SIKeyboard &last_kb,
                                  int si_key, auto ...imgui_keys)
{
	auto pressed      = false;
	auto pressed_last = false;

	for (auto i = 0; i < 3; i++) {
		pressed      = pressed      || kb.keys[i]      == si_key;
		pressed_last = pressed_last || last_kb.keys[i] == si_key;
	}

	auto &io = ImGui::GetIO();

	if (pressed && !pressed_last)
		(io.AddKeyEvent(imgui_keys, true), ...);
	else if (!pressed && pressed_last)
		(io.AddKeyEvent(imgui_keys, false), ...);
}

static void ImGui_ImplGC_CheckKeyboard(s32 chan)
{
	auto *bd = ImGui_ImplGC_GetBackendData();
	const auto &kb = bd->keyboard[chan];
	const auto &last_kb = bd->last_keyboard[chan];

	// Some of these are mapped differently due to keyboard layout
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_HOME,           ImGuiKey_Home);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_END,            ImGuiKey_End);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_PGUP,           ImGuiKey_PageUp);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_PGDN,           ImGuiKey_PageDown);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_SCROLLLOCK,     ImGuiKey_ScrollLock);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_A,              ImGuiKey_A);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_B,              ImGuiKey_B);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_C,              ImGuiKey_C);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_D,              ImGuiKey_D);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_E,              ImGuiKey_E);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F,              ImGuiKey_F);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_G,              ImGuiKey_G);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_H,              ImGuiKey_H);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_I,              ImGuiKey_I);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_J,              ImGuiKey_J);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_K,              ImGuiKey_K);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_L,              ImGuiKey_L);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_M,              ImGuiKey_M);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_N,              ImGuiKey_N);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_O,              ImGuiKey_O);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_P,              ImGuiKey_P);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_Q,              ImGuiKey_Q);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_R,              ImGuiKey_R);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_S,              ImGuiKey_S);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_T,              ImGuiKey_T);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_U,              ImGuiKey_U);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_V,              ImGuiKey_V);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_W,              ImGuiKey_W);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_X,              ImGuiKey_X);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_Y,              ImGuiKey_Y);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_Z,              ImGuiKey_Z);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_1,              ImGuiKey_1);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_2,              ImGuiKey_2);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_3,              ImGuiKey_3);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_4,              ImGuiKey_4);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_5,              ImGuiKey_5);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_6,              ImGuiKey_6);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_7,              ImGuiKey_7);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_8,              ImGuiKey_8);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_9,              ImGuiKey_9);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_0,              ImGuiKey_0);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_MINUS,          ImGuiKey_Minus);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_PLUS,           ImGuiKey_GraveAccent);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_PRINTSCR,       ImGuiKey_PrintScreen);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_BRACE_OPEN,     ImGuiKey_Apostrophe);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_BRACE_CLOSE,    ImGuiKey_LeftBracket);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_COLON,          ImGuiKey_Equal);

	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_HASH,           ImGuiKey_RightBracket);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_COMMA,          ImGuiKey_Comma);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_PERIOD,         ImGuiKey_Period);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_QUESTIONMARK,   ImGuiKey_Slash);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_INTERNATIONAL1, ImGuiKey_Backslash);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F1,             ImGuiKey_F1);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F2,             ImGuiKey_F2);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F3,             ImGuiKey_F3);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F4,             ImGuiKey_F4);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F5,             ImGuiKey_F5);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F6,             ImGuiKey_F6);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F7,             ImGuiKey_F7);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F8,             ImGuiKey_F8);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F9,             ImGuiKey_F9);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F10,            ImGuiKey_F10);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F11,            ImGuiKey_F11);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_F12,            ImGuiKey_F12);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_ESC,            ImGuiKey_Escape);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_INSERT,         ImGuiKey_Insert);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_DELETE,         ImGuiKey_Delete);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_TILDE,          ImGuiKey_Semicolon);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_BACKSPACE,      ImGuiKey_Backspace);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_TAB,            ImGuiKey_Tab);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_CAPSLOCK,       ImGuiKey_CapsLock);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_LEFTSHIFT,      ImGuiKey_LeftShift,
	                                                       ImGuiKey_ModShift);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_RIGHTSHIFT,     ImGuiKey_RightShift,
	                                                       ImGuiKey_ModShift);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_LEFTCONTROL,    ImGuiKey_LeftCtrl,
	                                                       ImGuiKey_ModCtrl);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_RIGHTALT,       ImGuiKey_RightAlt,
	                                                       ImGuiKey_ModAlt);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_LEFTWINDOWS,    ImGuiKey_LeftSuper,
	                                                       ImGuiKey_ModSuper);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_SPACE,          ImGuiKey_Space);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_RIGHTWINDOWS,   ImGuiKey_RightSuper,
	                                                       ImGuiKey_ModSuper);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_MENU,           ImGuiKey_Menu);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_LEFTARROW,      ImGuiKey_LeftArrow);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_DOWNARROW,      ImGuiKey_DownArrow);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_UPARROW,        ImGuiKey_UpArrow);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_RIGHTARROW,     ImGuiKey_RightArrow);
	ImGui_ImplGC_CheckKey(kb, last_kb, KEY_ENTER,          ImGuiKey_Enter);

	bd->last_keyboard[chan] = kb;
}

static void ImGui_ImplGC_PollKeyboard(s32 chan)
{
	auto *bd = ImGui_ImplGC_GetBackendData();

	if (SI_GetType(chan) != SI_GC_KEYBOARD) {
		bd->keyboard[chan]      = { 0 };
		bd->last_keyboard[chan] = { 0 };
		return;
	}

	// Request keyboard inputs
	auto cmd_direct = 0x54000000;
	SI_Transfer(chan, &cmd_direct, 1, &bd->keyboard[chan], sizeof(SIKeyboard),
	            [](s32, u32) {}, 0);

	ImGui_ImplGC_CheckKeyboard(chan);
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

static void ImGui_ImplGC_PollPad(s32 chan)
{
	auto *bd = ImGui_ImplGC_GetBackendData();
	const auto &status = HSD_PadMasterStatus[chan];

	if (status.err != 0) {
		bd->last_pad[chan] = { 0 };
		return;
	}

	auto &io = ImGui::GetIO();
	const auto &last_pad = bd->last_pad[chan];

	ImGui_ImplGC_Pad pad = { .buttons = status.buttons };
	pad.instant_buttons  = (pad.buttons ^ last_pad.buttons) & pad.buttons;
	pad.released_buttons = (pad.buttons ^ last_pad.buttons) & ~pad.buttons;

	// Apply deadzones
	if (std::abs(status.stick.x) > .2750f)
		pad.stick.x = status.stick.x;
	if (std::abs(status.stick.y) > .2750f)
		pad.stick.y = status.stick.y;
	if (std::abs(status.cstick.x) > .2750f)
		pad.cstick.x = status.cstick.x;
	if (std::abs(status.cstick.y) > .2750f)
		pad.cstick.y = status.cstick.y;
	if (status.analog_l > .3000f)
		pad.analog_l = status.analog_l;
	if (status.analog_r > .3000f)
		pad.analog_r = status.analog_r;

	ImGui_ImplGC_CheckButton(pad, Button_Start, ImGuiKey_GamepadStart);
	ImGui_ImplGC_CheckButton(pad, Button_Y,     ImGuiKey_GamepadFaceUp);
	ImGui_ImplGC_CheckButton(pad, Button_A,     ImGuiKey_GamepadFaceDown);
	ImGui_ImplGC_CheckButton(pad, Button_X,     ImGuiKey_GamepadFaceLeft);
	ImGui_ImplGC_CheckButton(pad, Button_B,     ImGuiKey_GamepadFaceRight);
	ImGui_ImplGC_CheckButton(pad, Button_Z,     ImGuiKey_GamepadR1);

	if (((pad.buttons ^ last_pad.buttons) & Button_L) || pad.analog_l != last_pad.analog_l)
		io.AddKeyAnalogEvent(ImGuiKey_GamepadL2, pad.buttons & Button_L, pad.analog_l);
	if (((pad.buttons ^ last_pad.buttons) & Button_R) || pad.analog_r != last_pad.analog_r)
		io.AddKeyAnalogEvent(ImGuiKey_GamepadR2, pad.buttons & Button_R, pad.analog_r);

	// Register control stick inputs as dpad inputs for navigation
	ImGui_ImplGC_CheckAnalog(pad.stick.x,  last_pad.stick.x,  ImGuiKey_GamepadDpadRight,
	                                                          ImGuiKey_GamepadDpadLeft);
	ImGui_ImplGC_CheckAnalog(pad.stick.y,  last_pad.stick.y,  ImGuiKey_GamepadDpadUp,
	                                                          ImGuiKey_GamepadDpadDown);

	// Register cstick inputs as left stick for scrolling
	ImGui_ImplGC_CheckAnalog(pad.cstick.x, last_pad.cstick.x, ImGuiKey_GamepadLStickRight,
	                                                          ImGuiKey_GamepadLStickLeft);
	ImGui_ImplGC_CheckAnalog(pad.cstick.y, last_pad.cstick.y, ImGuiKey_GamepadLStickUp,
	                                                          ImGuiKey_GamepadLStickDown);

	bd->last_pad[chan] = pad;
}

static void ImGui_ImplGC_TextForKeys(int ch, int key)
{
	if (ImGui::IsKeyPressed(key, true))
		ImGui::GetIO().AddInputCharacter(ch);
}

static void ImGui_ImplGC_TextForKeys(int ch, int key_start, int key_end)
{
	for (auto key = key_start; key <= key_end; key++)
		ImGui_ImplGC_TextForKeys(key - key_start + ch, key);
}

static void ImGui_ImplGC_TextForKeys(const char *str, int key_start, int key_end)
{
	for (auto key = key_start; key <= key_end; key++)
		ImGui_ImplGC_TextForKeys(str[key - key_start], key);
}

static void ImGui_ImplGC_AddTextEvents()
{
	ImGui_ImplGC_TextForKeys(' ', ImGuiKey_Space);

	if (ImGui::IsKeyDown(ImGuiKey_ModShift)) {
		ImGui_ImplGC_TextForKeys('A',            ImGuiKey_A,          ImGuiKey_Z);
		ImGui_ImplGC_TextForKeys(")!@#$%^&*(",   ImGuiKey_0,          ImGuiKey_9);
		ImGui_ImplGC_TextForKeys("\"<_>?:+{|}~", ImGuiKey_Apostrophe, ImGuiKey_GraveAccent);
	} else {
		ImGui_ImplGC_TextForKeys('a',            ImGuiKey_A,          ImGuiKey_Z);
		ImGui_ImplGC_TextForKeys('0',            ImGuiKey_0,          ImGuiKey_9);
		ImGui_ImplGC_TextForKeys("',-./;=[\\]`", ImGuiKey_Apostrophe, ImGuiKey_GraveAccent);
	}
}

void ImGui_ImplGC_NewFrame()
{
	auto *bd = ImGui_ImplGC_GetBackendData();
	IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplSDL2_Init()?");

	for (auto chan = 0; chan < 4; chan++) {
		ImGui_ImplGC_PollPad(chan);
		ImGui_ImplGC_PollKeyboard(chan);
	}

	ImGui_ImplGC_AddTextEvents();
}