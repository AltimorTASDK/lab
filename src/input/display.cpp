#include "os/os.h"
#include "os/serial.h"
#include "os/vi.h"
#include "hsd/pad.h"
#include "melee/action_state.h"
#include "melee/constants.h"
#include "melee/player.h"
#include "console/console.h"
#include "imgui/draw.h"
#include "input/poll.h"
#include "player/events.h"
#include "util/hooks.h"
#include "util/math.h"
#include "util/ring_buffer.h"
#include "util/vector.h"
#include "util/melee/pad.h"
#include <imgui.h>
#include <ogc/machine/asm.h>

constexpr auto INPUT_BUFFER_SIZE = MAX_POLLS_PER_FRAME * PAD_QNUM;
constexpr auto ACTION_HISTORY = 16;
constexpr auto ACTION_DISPLAY_TIME = 240;

struct saved_input {
	u8 qwrite;
	SIPadStatus status;
};

struct processed_input {
	u32 buttons;
	u32 pressed;
	u32 released;
	vec2 stick;
	vec2 cstick;

	processed_input(const Player *player, const SIPadStatus &status) :
		buttons(status.buttons),
		pressed((status.buttons ^ player->input.held_buttons) & status.buttons),
		released((status.buttons ^ player->input.held_buttons) & ~status.buttons),
		stick(convert_hw_coords(status.stick)),
		cstick(convert_hw_coords(status.cstick))
	{
	}
};

struct action_type {
	const char *name;
	// Action to time relative to
	const action_type *base_action = nullptr;
	// Predicate to detect state/inputs that trigger this action (before PlayerThink_Input)
	// Returns 0 or an arbitrary mask corresponding to the input method (e.g. X/Y/Up for jump)
	int(*input_predicate)(const Player *player, const processed_input &input);
	// Predicate to detect whether the action succeeded (after PlayerThink_Input)
	bool(*success_predicate)(const Player *player);
	// Array of input names corresponding to bits in mask returned by input_predicate
	const char *input_names[];
};

struct action {
	const action_type *type;
	// Number of frames between input for this and base action
	float frame_delta;
	// Number of poll with input for this action
	size_t poll_index;
	// Video frame action was performed on
	u32 frame;
	// Bit returned by action_type::input_predicate
	u8 input_type;
	bool success = false;
	u8 port;
};

namespace action_type_definitions {

static bool check_up_smash(const Player *player, const processed_input &input)
{
	return input.stick.y >= plco->y_smash_threshold &&
	       player->input.stick_y_hold_time < plco->y_smash_frames;
}

static const action_type jump = {
	.name = "Jump",
	.input_predicate = [](const Player *player, const processed_input &input) {
		if (player->airborne || player->action_state == AS_KneeBend)
			return 0;

		return ((input.pressed & Button_X)    ? (1 << 0) : 0) |
		       ((input.pressed & Button_Y)    ? (1 << 1) : 0) |
		       (check_up_smash(player, input) ? (1 << 2) : 0);
	},
	.success_predicate = [](const Player *player) {
		return player->action_state == AS_KneeBend;
	},
	.input_names = { "X", "Y", "Up" }
};

static const action_type airdodge = {
	.name = "Air Dodge",
	.base_action = &jump,
	.input_predicate = [](const Player *player, const processed_input &input) {
		if (!player->airborne && player->action_state != AS_KneeBend)
			return 0;

		return ((input.pressed & Button_L) ? (1 << 0) : 0) |
		       ((input.pressed & Button_R) ? (1 << 1) : 0);
	},
	.success_predicate = [](const Player *player) {
		return player->action_state == AS_EscapeAir;
	},
	.input_names = { "L", "R" }
};

} // action_type_definitions

static const action_type *action_types[] = {
	&action_type_definitions::jump,
	&action_type_definitions::airdodge,
};

constexpr auto action_type_count = std::extent_v<decltype(action_types)>;
static ring_buffer<saved_input, INPUT_BUFFER_SIZE> input_buffer[4];
static ring_buffer<action, ACTION_HISTORY> action_buffer;
static u32 last_confirmed_action[4];

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat == 0)
		input_buffer[chan].add({ .qwrite = HSD_PadLibData.qwrite, .status = status });
});

static u32 detect_action_for_input(const Player *player, const processed_input &input,
                                   size_t poll_index, const action_type &type, u32 input_mask)
{
	const auto mask = type.input_predicate(player, input) & input_mask;

	if (mask == 0)
		return 0;

	auto frame_delta = -1.f;

	if (type.base_action != nullptr) {
		// Find base action in buffer
		for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
			const auto *action = action_buffer.head(offset);

			if (action->type == type.base_action) {
				const auto poll_delta = poll_index - action->poll_index;
				frame_delta = (float)poll_delta / Si.poll.y;
				break;
			}
		}
	}

	// Add the action multiple times if triggered multiple times in one poll
	auto tmp = mask;

	while (tmp != 0) {
		const auto input_type = __builtin_ctz(tmp);
		tmp &= ~(1 << input_type);

		action_buffer.add({
			.type        = &type,
			.frame_delta = frame_delta,
			.poll_index  = poll_index,
			.frame       = VIGetRetraceCount(),
			.input_type  = (u8)input_type,
			.port        = player->port
		});
	}

	return mask;
}

EVENT_HANDLER(events::player::think::input::pre, [](Player *player)
{
	if (Player_IsCPU(player))
		return;

	const auto &buffer = input_buffer[player->port];

	// Only use polls corresponding to this frame
	const auto queue_index = mod(HSD_PadLibData.qread - 1, PAD_QNUM);

	// Head index must be saved because the interrupt can change it
	const auto head_index = buffer.head_index();
	const auto stored = std::min(head_index + 1, buffer.capacity());

	// Find where the polls for this frame begin and end
	const auto tail_index = head_index + 1 - stored;
	const auto invalid_index = head_index + 1;
	size_t start_index = tail_index;
	size_t end_index = invalid_index;

	for (size_t offset = 0; offset < stored; offset++) {
		const auto *input = buffer.get(head_index - offset);

		if (input == nullptr)
			continue;

		if (end_index == invalid_index) {
			if (input->qwrite == queue_index)
				end_index = head_index - offset;
		} else if (input->qwrite != queue_index) {
			start_index = head_index - offset + 1;
			break;
		}
	}

	if (end_index == invalid_index) {
		OSReport("Failed to find polls corresponding to current frame\n");
		return;
	}

	// Don't detect the same inputs for the same action repeatedly in one frame
	u32 detected_inputs[action_type_count] = { 0 };

	for (auto index = start_index; index <= end_index; index++) {
		const auto *input = buffer.get(index);

		if (input == nullptr)
			continue;

		const auto processed = processed_input(player, input->status);

		for (size_t type_index = 0; type_index < action_type_count; type_index++) {
			const auto &type = *action_types[type_index];
			const auto mask = detect_action_for_input(player, processed, index, type,
			                                          ~detected_inputs[type_index]);
			detected_inputs[type_index] |= mask;
		}
	}
});

EVENT_HANDLER(events::player::think::input::post, [](Player *player)
{
	if (Player_IsCPU(player))
		return;

	const auto port = player->port;

	// Figure out which actions succeeded
	for (; last_confirmed_action[port] < action_buffer.count(); last_confirmed_action[port]++) {
		auto *action = action_buffer.get(last_confirmed_action[port]);

		if (action->port == port && action->type->success_predicate(player))
			action->success = true;
	}
});

EVENT_HANDLER(events::imgui::draw, []()
{
	ImGui::SetNextWindowPos({20, 20});
	ImGui::Begin("Inputs", nullptr, ImGuiWindowFlags_NoResize
	                              | ImGuiWindowFlags_AlwaysAutoResize
	                              | ImGuiWindowFlags_NoMove
	                              | ImGuiWindowFlags_NoInputs
	                              | ImGuiWindowFlags_NoDecoration
	                              | ImGuiWindowFlags_NoBackground);

	ImGui::BeginTable("Inputs", 2, 0, {320, 480 - 40});

	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		const auto *action = action_buffer.tail(offset);

		if (VIGetRetraceCount() - action->frame > ACTION_DISPLAY_TIME)
			continue;

                const char *input_name = action->type->input_names[action->input_type];

                ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (input_name != nullptr)
			ImGui::Text("%s (%s)\n", action->type->name, input_name);
		else
			ImGui::TextUnformatted(action->type->name);

		if (action->frame_delta != -1.f) {
			ImGui::TableNextColumn();
			ImGui::Text("%f", action->frame_delta);
		}
	}

	ImGui::EndTable();

	ImGui::End();
});