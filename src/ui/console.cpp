#include "os/os.h"
#include "hsd/cobj.h"
#include "hsd/gobj.h"
#include "hsd/video.h"
#include "melee/menu.h"
#include "event/event.h"
#include "imgui/draw.h"
#include "util/hash.h"
#include "ui/console.h"
#include <cstdarg>
#include <cstdio>
#include <imgui.h>

static constexpr auto HISTORY_LINES = 16;
static constexpr auto LINE_SIZE     = 80;

static char history_buf[HISTORY_LINES][LINE_SIZE];
static int history_idx = 0;
static char line_buf[LINE_SIZE];

static bool console_open;

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

        if (!events::console::cmd.fire(hash(cmd), line_buf))
                console::printf("Unrecognized command \"%s\"", cmd);
}

static int text_callback(ImGuiInputTextCallbackData *data)
{
        return data->EventChar != '`' && data->EventChar != '~';
}

static event_handler draw_handler(&events::imgui::draw, []()
{
        if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
                console_open = !console_open;

        if (!console_open)
                return;

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
	    ImGuiInputTextFlags_EnterReturnsTrue, text_callback)) {
                parse_line();
		line_buf[0] = '\0';
        }

	ImGui::End();
        ImGui::PopStyleVar();
});