#include "os/os.h"
#include "hsd/cobj.h"
#include "hsd/gobj.h"
#include "hsd/video.h"
#include "melee/menu.h"
#include "util/hash.h"
#include "util/hooks.h"
#include "imgui/backends/imgui_impl_gc.h"
#include "imgui/backends/imgui_impl_gx.h"
#include "ui/console.h"
#include <cstdarg>
#include <cstdio>
#include <imgui.h>

static constexpr auto HISTORY_LINES = 16;
static constexpr auto LINE_SIZE     = 80;

static char history_buf[HISTORY_LINES][LINE_SIZE];
static int history_idx = 0;
static char line_buf[LINE_SIZE];

void console::print(const char *line)
{
        strlcpy(history_buf[history_idx], line, HISTORY_LINES);
        history_idx = (history_idx + 1) % HISTORY_LINES;
}

void console::printf(const char *fmt, ...)
{
        va_list va;
        va_start(va, fmt);
        vsnprintf(history_buf[history_idx], LINE_SIZE, fmt, va);
        va_end(va);
        history_idx = (history_idx + 1) % HISTORY_LINES;
}

static void parse_line()
{
        if (line_buf[0] == '\0')
                return;

        console::printf(">%s", line_buf);

        char cmd[33];
        if (sscanf(line_buf, "%32s", cmd) != 1) {
                console::print("Failed to parse command");
                return;
        }

        if (!FIRE_EVENT("console.cmd", hash(cmd), line_buf))
                console::printf("Unrecognized command \"%s\"", cmd);
}

static void draw(HSD_GObj *gobj, u32 pass)
{
	if (pass != HSD_RP_BOTTOMHALF)
		return;

	ImGui_ImplGC_NewFrame();
	ImGui_ImplGX_NewFrame();
	ImGui::NewFrame();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
	ImGui::SetNextWindowPos({0, 0});
	ImGui::SetNextWindowSize({640, 0});
	ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoResize
	                               | ImGuiWindowFlags_NoMove
	                               | ImGuiWindowFlags_NoNav
	                               | ImGuiWindowFlags_NoDecoration);
        ImGui::PopStyleVar();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2, 2});
	ImGui::BeginChild("history", {640, 200}, true, ImGuiWindowFlags_NoDecoration);
        ImGui::PopStyleVar();

        // Ensure contents always fill window
        ImGui::Dummy({640, 200});

        for (auto i = 0; i < HISTORY_LINES; i++) {
                const auto index = (history_idx + i) % HISTORY_LINES;
                ImGui::TextUnformatted(history_buf[index]);
        }

	ImGui::SetScrollHereY(1.f);
	ImGui::EndChild();

	ImGui::SetNextItemWidth(640);
	ImGui::SetKeyboardFocusHere();
	if (ImGui::InputText("##input", line_buf, IM_ARRAYSIZE(line_buf),
	    ImGuiInputTextFlags_EnterReturnsTrue)) {
                parse_line();
		line_buf[0] = '\0';
        }

	ImGui::End();
        ImGui::PopStyleVar();

	ImGui::Render();
        ImGui_ImplGX_RenderDrawData(ImGui::GetDrawData());
}

extern "C" void CSS_Setup();

HOOK(CSS_Setup, [&]()
{
	original();

	auto *gobj = GObj_Create(4, 5, 0x80);
	GObj_SetupGXLink(gobj, draw, 1, 0x80);
});