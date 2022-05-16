#include "os/os.h"
#include "hsd/cobj.h"
#include "hsd/gobj.h"
#include "hsd/video.h"
#include "melee/menu.h"
#include "console/console.h"
#include "event/event.h"
#include "imgui/draw.h"
#include "util/hash.h"
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
        strlcpy(history_buf[history_idx], line, LINE_SIZE);
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
        console::printf(">%s", line_buf);

        char arg_buf[LINE_SIZE];
        strcpy(arg_buf, line_buf);

        auto argc = 0;
        const char *argv[LINE_SIZE / 2];
        auto *arg_ptr = arg_buf;

        while (arg_ptr != nullptr) {
                // Null out spaces to separate args
                while (*arg_ptr == ' ')
                        *arg_ptr++ = '\0';

                if (*arg_ptr == '\0')
                        break;

                if (*arg_ptr == '\"') {
                        // TODO: Escaping
                        argv[argc++] = arg_ptr + 1;
                        arg_ptr = strchr(arg_ptr + 1, '\"');
                        if (arg_ptr != nullptr)
                                arg_ptr++;
                } else {
                        argv[argc++] = arg_ptr;
                        arg_ptr = strchr(arg_ptr + 1, ' ');
                }
        }

        if (argc == 0)
                return;

        if (!events::console::cmd.fire(hash(argv[0]), argc, argv))
                console::printf("Unrecognized command \"%s\"", argv[0]);
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