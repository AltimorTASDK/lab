#include "imgui/events.h"
#include "imgui/fonts.h"
#include <imgui.h>

#include "resources/fonts/cascadia_mono.ttf.h"

EVENT_HANDLER(events::imgui::init, []()
{
	auto &io = ImGui::GetIO();

	ImFontConfig font_config;
	font_config.FontDataOwnedByAtlas = false;

	auto *data = cascadia_mono_ttf_data;
	const auto size = sizeof(cascadia_mono_ttf_data);

	fonts::small  = io.Fonts->AddFontFromMemoryTTF(data, size, 12.f, &font_config);
	fonts::medium = io.Fonts->AddFontFromMemoryTTF(data, size, 16.f, &font_config);
	fonts::large  = io.Fonts->AddFontFromMemoryTTF(data, size, 20.f, &font_config);

	io.FontDefault = fonts::medium;
});