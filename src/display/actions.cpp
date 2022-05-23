#include "os/os.h"
#include "os/serial.h"
#include "os/vi.h"
#include "hsd/pad.h"
#include "melee/action_state.h"
#include "melee/constants.h"
#include "melee/player.h"
#include "console/console.h"
#include "match/events.h"
#include "imgui/events.h"
#include "imgui/fonts.h"
#include "input/poll.h"
#include "player/events.h"
#include "util/bitwise.h"
#include "util/hooks.h"
#include "util/math.h"
#include "util/ring_buffer.h"
#include "util/vector.h"
#include "util/melee/pad.h"
#include <imgui.h>
#include <ogc/machine/asm.h>
#include <tuple>

// How many recent inputs to remember
constexpr size_t INPUT_BUFFER_SIZE = MAX_POLLS_PER_FRAME * PAD_QNUM;
// How many recent actions to remember
constexpr size_t ACTION_BUFFER_SIZE = 32;
// How many actions to display
constexpr size_t ACTION_HISTORY = 10;
// Frame window to consider a duplicate action input a plink
constexpr float PLINK_WINDOW = 2.f;

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
	// Index in action_type_definitions
	size_t index;
	// Action to time relative to
	const action_type *base_action;
	// If true, ignore state_predicate if last action input is within PLINK_WINDOW frames
	bool plinkable;
	// Predicate to detect prerequisite player state for this action (before PlayerThink_Input)
	bool(*state_predicate)(const Player *player);
	// Predicate to detect inputs that trigger this action (before PlayerThink_Input)
	// Returns 0 or an arbitrary mask corresponding to the input method (e.g. X/Y/Up for jump)
	int(*input_predicate)(const Player *player, const processed_input &input);
	// Predicate to detect whether the action succeeded (after PlayerThink_Input)
	bool(*success_predicate)(const Player *player);
	// Predicate to detect when this can no longer be used as a valid base action
	bool(*end_predicate)(const Player *player);
	// Array of input names corresponding to bits in mask returned by input_predicate
	const char *input_names[];
};

struct action_entry {
	const action_type *type;
	// Action to time relative to
	const action_entry *base_action;
	// Number of poll with input for this action
	size_t poll_index;
	// Video frame action was performed on
	unsigned int frame;
	// Whether this can still be used as a valid base action
	bool active = true;
	// Whether "success" has been set
	bool confirmed = false;
	// Whether the character actually performed the action
	bool success = false;
	// Bit returned by action_type::input_predicate
	u8 input_type;
	// Controller port
	u8 port;
};

namespace action_type_definitions {

bool check_up_smash(const Player *player, const processed_input &input)
{
	return input.stick.y >= plco->y_smash_threshold &&
	       player->input.stick_y_hold_time < plco->y_smash_frames;
}

bool check_down_b(const Player *player, const processed_input &input)
{
	switch (player->action_state) {
	default:
		if (!player->airborne && std::abs(input.stick.x) >= plco->x_special_threshold)
			return false;
	case AS_SquatWait:
	case AS_SquatRv:
		return (input.pressed & Button_B) && input.stick.y <= -plco->y_special_threshold;
	}
}

static const action_type jump = {
	.name = "Jump",
	.state_predicate = [](const Player *player) {
		return !player->airborne && player->action_state != AS_KneeBend;
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_up_smash(player, input));
	},
	.success_predicate = [](const Player *player) {
		return player->action_state == AS_KneeBend;
	},
	.end_predicate = [](const Player *player) {
		return !player->airborne && player->action_state != AS_KneeBend;
	},
	.input_names = { "X", "Y", "Up" }
};

static const action_type airdodge = {
	.name = "Air Dodge",
	.base_action = &jump,
	.plinkable = true,
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_L,
		                     input.pressed & Button_R);
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
static ring_buffer<action_entry, ACTION_BUFFER_SIZE> action_buffer;
static unsigned int draw_frames;

[[gnu::constructor]] static void set_action_type_indices()
{
	for (size_t i = 0; i < action_type_count; i++)
		const_cast<action_type*>(action_types[i])->index = i;
}

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat == 0)
		input_buffer[chan].add({ .qwrite = HSD_PadLibData.qwrite, .status = status });
});

static void detect_action_for_input(const Player *player, const processed_input &input,
                                    size_t poll_index, size_t type_index, u32 *detected_inputs)
{
	const auto &type = *action_types[type_index];

	// Don't detect the same inputs for the same action repeatedly in one frame
	auto mask = type.input_predicate(player, input) & ~detected_inputs[type_index];

	if (mask == 0)
		return;

	const action_entry *base_action = nullptr;
	auto check_state = true;

	// Find base action in buffer
	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		const auto *action = action_buffer.head(offset);

		if (action->type == type.base_action && action->active && base_action == nullptr)
			base_action = action;

		if (!type.plinkable || !check_state)
			continue;

		// Don't check state for plinked inputs
		const auto poll_delta = poll_index - action->poll_index;
		const auto frame_delta = (float)poll_delta / Si.poll.y;
		check_state = frame_delta > PLINK_WINDOW;
	}

	if (type.base_action != nullptr) {
		// Base action is required
		if (base_action == nullptr)
			return;

		// Don't check state if base input on same frame
		if (detected_inputs[base_action->type->index] != 0)
			check_state = false;
	}

	// Check action prerequisites unless otherwise specified
	if (check_state && type.state_predicate != nullptr && !type.state_predicate(player))
		return;

	detected_inputs[type_index] |= mask;

	// Add the action multiple times if triggered multiple times in one poll
	while (mask != 0) {
		const auto input_type = __builtin_ctz(mask);
		mask &= ~(1 << input_type);

		action_buffer.add({
			.type        = &type,
			.base_action = base_action,
			.poll_index  = poll_index,
			.frame       = draw_frames,
			.input_type  = (u8)input_type,
			.port        = player->port
		});
	}
}

static std::tuple<size_t, size_t> find_polls_for_frame(u8 port)
{
	const auto &buffer = input_buffer[port];

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
		return std::make_tuple(1, 0);
	}

	return std::make_tuple(start_index, end_index);
}

EVENT_HANDLER(events::player::think::input::pre, [](Player *player)
{
	if (Player_IsCPU(player))
		return;

	// Store masks for which input types were detected for each action type
	u32 detected_inputs[action_type_count] = { 0 };

	auto [start_index, end_index] = find_polls_for_frame(player->port);

	for (auto poll_index = start_index; poll_index <= end_index; poll_index++) {
		const auto *input = input_buffer[player->port].get(poll_index);

		if (input == nullptr)
			continue;

		const auto processed = processed_input(player, input->status);

		for (size_t type_index = 0; type_index < action_type_count; type_index++) {
			detect_action_for_input(player, processed, poll_index, type_index,
			                        detected_inputs);
		}
	}
});

EVENT_HANDLER(events::player::think::input::post, [](Player *player)
{
	if (Player_IsCPU(player))
		return;

	const auto port = player->port;

	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		const auto index = action_buffer.tail_index(offset);
		auto *action = action_buffer.get(index);
		const auto *type = action->type;

		if (action->port != port)
			continue;

		// Check if action can still be used as base
		if (action->active && type->end_predicate != nullptr)
			action->active = !type->end_predicate(player);

		// Figure out which actions succeeded
		if (!action->confirmed) {
			action->success = type->success_predicate(player);
			action->confirmed = true;
		}
	}
});

EVENT_HANDLER(events::imgui::draw, []()
{
	ImGui::SetNextWindowPos({10, 30});
	ImGui::Begin("Inputs", nullptr, ImGuiWindowFlags_NoResize
	                              | ImGuiWindowFlags_NoMove
	                              | ImGuiWindowFlags_NoInputs
	                              | ImGuiWindowFlags_NoDecoration
	                              | ImGuiWindowFlags_NoBackground
	                              | ImGuiWindowFlags_AlwaysAutoResize);

	ImGui::BeginTable("Inputs", 2, ImGuiTableFlags_SizingFixedFit);

	const auto display_count = std::min(action_buffer.stored(), ACTION_HISTORY);

	for (size_t offset = 0; offset < display_count; offset++) {
		const auto *action = action_buffer.head(offset);
                const auto *base_action = action->base_action;
                const auto *input_name = action->type->input_names[action->input_type];

                ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (action->success)
			ImGui::TextColored({.2f, 1.f, .2f, 1.f}, "✔");
		else
			ImGui::TextColored({1.f, .2f, .2f, 1.f}, "❌");

		if (base_action != nullptr) {
			ImGui::SameLine();
			ImGui::Text("%s ->", base_action->type->name);
		}

		ImGui::SameLine();
		ImGui::TextUnformatted(action->type->name);

		if (input_name != nullptr) {
			ImGui::SameLine();
			ImGui::Text("(%s)", input_name);
		}

		if (base_action != nullptr) {
			const auto poll_delta = action->poll_index - base_action->poll_index;
			const auto frame_delta = (float)poll_delta / Si.poll.y;
			ImGui::TableNextColumn();
			ImGui::Text("%.02ff", frame_delta);
		} else {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Dummy({0, 5});
		}
	}

	ImGui::EndTable();

	ImGui::End();

	draw_frames++;
});

EVENT_HANDLER(events::match::exit, []()
{
	action_buffer.clear();
});