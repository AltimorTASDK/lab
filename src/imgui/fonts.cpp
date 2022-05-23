#include "imgui/events.h"
#include "imgui/fonts.h"
#include <imgui.h>

#include "resources/fonts/cascadia_mono.ttf.h"

// Unicode ranges
const auto *glyphs = (const ImWchar*)u"\u0020\u00FF✔✔❌❌";

template<size_t N>
static ImFont *create_font(unsigned char (&data)[N], float size_pixels, ImFontConfig *config)
{
	return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, N, size_pixels, config);
}

EVENT_HANDLER(events::imgui::init, []()
{
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;
	config.GlyphRanges = glyphs;

	fonts::small  = create_font(cascadia_mono_ttf_data, 12.f, &config);
	fonts::medium = create_font(cascadia_mono_ttf_data, 16.f, &config);
	fonts::large  = create_font(cascadia_mono_ttf_data, 20.f, &config);

	ImGui::GetIO().FontDefault = fonts::medium;
});