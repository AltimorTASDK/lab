#include "os/os.h"
#include "os/serial.h"
#include "os/vi.h"
#include "hsd/pad.h"
#include "melee/action_state.h"
#include "melee/constants.h"
#include "melee/player.h"
#include "melee/characters/fox.h"
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
constexpr size_t PLINK_WINDOW = 3;

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
		stick(convert_hw_coords(status.stick)),
		cstick(convert_hw_coords(status.cstick))
	{
		if (buttons & Button_Z) {
			buttons |= Button_A;
			buttons |= Button_AnalogLR;
		}

		if (std::max(status.analog_l, status.analog_r) >= LR_DEADZONE)
			buttons |= Button_AnalogLR;

		pressed  = (buttons ^ player->input.held_buttons) &  buttons;
		released = (buttons ^ player->input.held_buttons) & ~buttons;
	}
};

struct action_type {
	const char *name;
	// Index in action_type_definitions
	size_t index;
	// If true, ignore state/base action end if last action input is within PLINK_WINDOW frames
	bool plinkable;
	// Check whether a previous action is a suitable base action (relative timing to) for this
	bool(*is_base_action)(const struct action_entry *action);
	// Predicate to detect prerequisite player state for this action (before PlayerThink_Input)
	bool(*state_predicate)(const Player *player, const struct action_entry *base);
	// Predicate to detect inputs that trigger this action (before PlayerThink_Input)
	// Returns 0 or an arbitrary mask corresponding to the input method (e.g. X/Y/Up for jump)
	int(*input_predicate)(const Player *player, const processed_input &input);
	// Predicate to detect whether the action succeeded (after PlayerThink_Input)
	// new_state is AS_None if there was no AS change
	bool(*success_predicate)(const Player *player, u32 new_state);
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

enum class state_type {
	ground,
	air,
	ledge
};

state_type get_state_type(const Player *player)
{
	if (player->action_state >= AS_CliffCatch && player->action_state <= AS_CliffJumpQuick2)
		return state_type::ledge;

	// Consider inputs in jumpsquat to be attempted airborne inputs
	if (player->action_state == AS_KneeBend || player->airborne)
		return state_type::air;

	return state_type::ground;
}

bool is_on_ledge(const Player *player) { return get_state_type(player) == state_type::ledge;  }
bool is_grounded(const Player *player) { return get_state_type(player) == state_type::ground; }
bool is_airborne(const Player *player) { return get_state_type(player) == state_type::air;    }

int get_stick_x_hold_time(const Player *player, const processed_input &input)
{
	// Determine next stick_x_hold_time value
	const auto sign = std::copysign(1.f, input.stick.x);

	if (input.stick.x * sign < plco->stick_hold_threshold.x)
		return 0xFE;

	if (player->input.stick.x * sign < plco->stick_hold_threshold.x)
		return 0;

	return player->input.stick_x_hold_time + 1;
}

int get_stick_y_hold_time(const Player *player, const processed_input &input)
{
	// Determine next stick_y_hold_time value
	const auto sign = std::copysign(1.f, input.stick.y);

	if (input.stick.y * sign < plco->stick_hold_threshold.y)
		return 0xFE;

	if (player->input.stick.y * sign < plco->stick_hold_threshold.y)
		return 0;

	return player->input.stick_y_hold_time + 1;
}

bool check_fsmash(const Player *player, const processed_input &input)
{
	return input.stick.x * player->direction >= plco->x_smash_threshold &&
	       get_stick_x_hold_time(player, input) < plco->x_smash_frames;
}

bool check_fsmash_instant(const Player *player, const processed_input &input)
{
	return input.stick.x * player->direction >= plco->x_smash_threshold &&
	       player->input.stick.x * player->direction < plco->x_smash_threshold;
}

bool check_bsmash(const Player *player, const processed_input &input)
{
	return -input.stick.x * player->direction >= plco->x_smash_threshold &&
	       get_stick_x_hold_time(player, input) < plco->x_smash_frames;
}

bool check_bsmash_instant(const Player *player, const processed_input &input)
{
	return -input.stick.x * player->direction >= plco->x_smash_threshold &&
	       -player->input.stick.x * player->direction < plco->x_smash_threshold;
}

bool check_usmash(const Player *player, const processed_input &input)
{
	return input.stick.y >= plco->y_smash_threshold &&
	       get_stick_y_hold_time(player, input) < plco->y_smash_frames;
}

bool check_usmash_instant(const Player *player, const processed_input &input)
{
	return input.stick.y >= plco->y_smash_threshold &&
	       player->input.stick.y < plco->y_smash_threshold;
}

bool check_dsmash(const Player *player, const processed_input &input)
{
	return -input.stick.y >= plco->y_smash_threshold &&
	       get_stick_y_hold_time(player, input) < plco->y_smash_frames;
}

bool check_dsmash_instant(const Player *player, const processed_input &input)
{
	return -input.stick.y >= plco->y_smash_threshold &&
	       -player->input.stick.y < plco->y_smash_threshold;
}

bool check_down_b(const Player *player, const processed_input &input)
{
	switch (player->action_state) {
	default:
		if (!is_airborne(player) && std::abs(input.stick.x) >= plco->x_special_threshold)
			return false;
	case AS_SquatWait:
	case AS_SquatRv:
		return (input.pressed & Button_B) && input.stick.y <= -plco->y_special_threshold;
	}
}

u32 check_aerial(const Player *player, const processed_input &input)
{
	const auto &last_cstick = player->input.cstick;
	const auto threshold_x = plco->aerial_threshold_x;
	const auto threshold_y = plco->aerial_threshold_y;

	vec2 stick;

	if ((std::abs(last_cstick.x) < threshold_x && std::abs(input.cstick.x) >= threshold_x) ||
	    (std::abs(last_cstick.y) < threshold_y && std::abs(input.cstick.y) >= threshold_y)) {
		stick = input.cstick;
	} else if (input.pressed & Button_A) {
		// Assume Z presses during an aerial are L cancels
		if (!player->iasa &&
		    player->action_state >= AS_AttackAirN &&
		    player->action_state <= AS_AttackAirLw &&
		    (input.pressed & Button_Z)) {
			return AS_None;
		}

		stick = input.stick;

		if (std::abs(stick.x) < threshold_x && std::abs(stick.y) < threshold_y)
			return AS_AttackAirN;
	} else {
		return AS_None;
	}

	if (get_stick_angle(stick) > plco->angle_50d)
		return stick.y > 0 ? AS_AttackAirHi : AS_AttackAirLw;
	else
		return stick.x * player->direction > 0 ? AS_AttackAirF : AS_AttackAirB;
}

extern const action_type shine;

const action_type dash = {
	.name = "Dash",
	.is_base_action = [](const action_entry *action) {
		return action->type == &dash;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_fsmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Dash;
	},
	.input_names = { nullptr }
};

const action_type pivot = {
	.name = "Pivot",
	.is_base_action = [](const action_entry *action) {
		return action->type == &dash || action->type == &pivot;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_bsmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == AS_Turn;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Turn && player->action_state != AS_Dash;
	},
	.input_names = { nullptr }
};

const action_type jump = {
	.name = "Jump",
	.is_base_action = [](const action_entry *action) {
		return action->type == &shine || action->type == &dash;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return !is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_usmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == AS_KneeBend;
	},
	.end_predicate = [](const Player *player) {
		return !is_airborne(player);
	},
	.input_names = { "X", "Y", "Up" }
};

const action_type dj = {
	.name = "DJ",
	.is_base_action = [](const action_entry *action) {
		return action->type == &jump || action->type == &dj || action->type == &shine;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_usmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == AS_JumpAerialF ||
		       new_state == AS_JumpAerialB;
	},
	.end_predicate = [](const Player *player) {
		return !is_airborne(player);
	},
	.input_names = { "X", "Y", "Up" }
};

template<string_literal name, u32 state>
const action_type aerial = {
	.name = name.value,
	.is_base_action = [](const action_entry *action) {
		return action->type == &jump || action->type == &dj;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_aerial(player, input) == state);
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == state;
	},
	.input_names = { nullptr }
};

const action_type &nair = aerial<"Nair", AS_AttackAirN>;
const action_type &fair = aerial<"Fair", AS_AttackAirF>;
const action_type &bair = aerial<"Bair", AS_AttackAirB>;
const action_type &uair = aerial<"Uair", AS_AttackAirHi>;
const action_type &dair = aerial<"Dair", AS_AttackAirLw>;

const action_type airdodge = {
	.name = "Air Dodge",
	.plinkable = true,
	.is_base_action = [](const action_entry *action) {
		return action->type == &jump || action->type == &dj;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return base != nullptr || is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_L,
		                     input.pressed & Button_R);
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == AS_EscapeAir;
	},
	.input_names = { "L", "R" }
};

const action_type shine = {
	.name = "Shine",
	.is_base_action = [](const action_entry *action) {
		return action->type == &jump;
	},
	.state_predicate = [](const Player *player, const action_entry *base) {
		return !is_on_ledge(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_down_b(player, input));
	},
	.success_predicate = [](const Player *player, u32 new_state) {
		return new_state == AS_Fox_SpecialLwStart ||
		       new_state == AS_Fox_SpecialAirLwStart;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state < AS_Fox_SpecialLwStart ||
		       player->action_state > AS_Fox_SpecialAirLwTurn;
	},
	.input_names = { nullptr }
};

} // action_type_definitions

static const action_type *action_types[] = {
	&action_type_definitions::dash,
	&action_type_definitions::pivot,
	&action_type_definitions::jump,
	&action_type_definitions::dj,
	&action_type_definitions::nair,
	&action_type_definitions::fair,
	&action_type_definitions::bair,
	&action_type_definitions::uair,
	&action_type_definitions::dair,
	&action_type_definitions::airdodge,
	&action_type_definitions::shine
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

	const action_entry *base = nullptr;
	auto plinked = false;

	// Find base action in buffer
	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		const auto *action = action_buffer.head(offset);

		if (base == nullptr && type.is_base_action != nullptr) {
			// Count the 2nd input in a plink even if the base action ended
			if (type.is_base_action(action) && (action->active || plinked))
				base = action;
		}

		if (type.plinkable && !plinked) {
			// Check if this is the 2nd input in a plink
			const auto poll_delta = poll_index - action->poll_index;
			plinked = poll_delta <= PLINK_WINDOW * Si.poll.y;
		}
	}

	// Check action prerequisites unless plinked
	if (!plinked && type.state_predicate != nullptr && !type.state_predicate(player, base))
		return;

	detected_inputs[type_index] |= mask;

	// Add the action multiple times if triggered multiple times in one poll
	while (mask != 0) {
		const auto input_type = __builtin_ctz(mask);
		mask &= ~(1 << input_type);

		action_buffer.add({
			.type        = &type,
			.base_action = base,
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

EVENT_HANDLER(events::player::think::input::post, [](Player *player, u32 old_state)
{
	if (Player_IsCPU(player))
		return;

	const auto port = player->port;
	const auto new_state = player->action_state != old_state ? player->action_state : AS_None;

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
			action->success = type->success_predicate(player, new_state);
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