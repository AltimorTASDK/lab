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
#include "input/poll.h"
#include "player/events.h"
#include "util/bitwise.h"
#include "util/hooks.h"
#include "util/math.h"
#include "util/ring_buffer.h"
#include "util/vector.h"
#include "util/melee/ftcmd.h"
#include "util/melee/pad.h"
#include <imgui.h>
#include <ogc/machine/asm.h>
#include <tuple>

// How many recent inputs to remember
constexpr size_t INPUT_BUFFER_SIZE = MAX_POLLS_PER_FRAME * PAD_QNUM;
// How many recent actions to remember
constexpr size_t ACTION_BUFFER_SIZE = 64;
// How many actions to display
constexpr size_t ACTION_HISTORY = 10;
// Frame window to consider a duplicate action input a plink
constexpr size_t PLINK_WINDOW = 3;
// Frame window to consider actions to be intended one after another
constexpr size_t ACT_OUT_WINDOW = 3;

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

	processed_input() = default;

	processed_input(const Player *player, const SIPadStatus &status) :
		buttons(status.buttons),
		stick(convert_hw_coords(status.stick)),
		cstick(convert_hw_coords(status.cstick))
	{
		if (buttons & Button_Z)
			buttons |= Button_AnalogLR | Button_A;

		if (std::max(status.analog_l, status.analog_r) >= LR_DEADZONE)
			buttons |= Button_AnalogLR;

		pressed  = (buttons ^ player->input.held_buttons) &  buttons;
		released = (buttons ^ player->input.held_buttons) & ~buttons;
	}
};

struct action_type {
	const char *name;
	// If true, can only happen if a base action is found
	bool needs_base;
	// If true, ignore state/base action end if last action input is within PLINK_WINDOW frames
	bool plinkable;
	// If true, action must succeed to be active or displayed
	bool must_succeed;
	// How many frames to continue checking for action success for
	unsigned int success_window = 1;
	// How many frames to still consider action active after end_predicate returns true
	unsigned int end_delay;
	// Check whether a previous action is a suitable base action (relative timing to) for this
	bool(*is_base_action)(const struct action_entry *action, size_t poll_delta);
	// Predicate to detect prerequisite player state for this action (before PlayerThink_Input)
	bool(*state_predicate)(const Player *player, const struct action_entry *base,
	                                             size_t poll_delta);
	// Predicate to detect inputs that trigger this action (before PlayerThink_Input)
	// Returns 0 or an arbitrary mask corresponding to the input method (e.g. X/Y/Up for jump)
	int(*input_predicate)(const Player *player, const processed_input &input);
	// Like input_predicate, but receives input from base action.
	int(*base_input_predicate)(const Player *player, const processed_input &input,
	                                                 const processed_input &base_input);
	// Predicate to detect whether the action succeeded (after PlayerThink_Input)
	// new_state is AS_None if there was no AS change
	bool(*success_predicate)(const Player *player, s32 new_state);
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
	// Input this action was detected with
	processed_input input;
	// Video frame action was performed on
	unsigned int frame;
	// How many frames until action becomes inactive
	unsigned int end_timer;
	// Whether this can still be used as a valid base action
	bool active;
	// How many frames success has been checked for
	unsigned int success_timer;
	// Whether "success" has been set
	bool confirmed;
	// Whether the character actually performed the action
	bool success;
	// Bit returned by action_type::input_predicate
	u8 input_type;
	// Controller port
	u8 port;

	bool is_type(const auto &...types) const
	{
		return ((type == &types) || ...);
	}
};

namespace action_type_definitions {

enum class state_type {
	ground,
	air,
	ledge,
	knockdown
};

bool in_state(const Player *player, u32 state)
{
	return player->action_state == state;
}

bool in_state_range(const Player *player, u32 start, u32 end)
{
	return player->action_state >= start && player->action_state <= end;
}

bool is_char(const Player *player, auto ...characters)
{
	return ((player->character_id == characters) || ...);
}

state_type get_state_type(const Player *player)
{
	if (in_state_range(player, AS_CliffCatch, AS_CliffJumpQuick2))
		return state_type::ledge;

	// Consider inputs in jumpsquat to be attempted airborne inputs
	if (player->action_state == AS_KneeBend || player->airborne)
		return state_type::air;

	if (in_state_range(player, AS_DownBoundU, AS_DownDamageU))
		return state_type::knockdown;

	if (in_state_range(player, AS_DownBoundD, AS_DownDamageD))
		return state_type::knockdown;

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

bool check_fsmash_region(const Player *player, const processed_input &input)
{
	return input.stick.x * player->direction >= plco->x_smash_threshold;
}

bool check_fsmash(const Player *player, const processed_input &input)
{
	return check_fsmash_region(player, input) &&
	       get_stick_x_hold_time(player, input) < plco->x_smash_frames;
}

bool check_fsmash_instant(const Player *player, const processed_input &input)
{
	return check_fsmash(player, input) &&
	       player->input.stick.x * player->direction < plco->x_smash_threshold;
}

bool check_bsmash_region(const Player *player, const processed_input &input)
{
	return -input.stick.x * player->direction >= plco->x_smash_threshold;
}

bool check_bsmash(const Player *player, const processed_input &input)
{
	return check_bsmash_region(player, input) &&
	       get_stick_x_hold_time(player, input) < plco->x_smash_frames;
}

bool check_bsmash_instant(const Player *player, const processed_input &input)
{
	return check_bsmash(player, input) &&
	       -player->input.stick.x * player->direction < plco->x_smash_threshold;
}

bool check_xsmash(const Player *player, const processed_input &input)
{
	return std::abs(input.stick.x) >= plco->x_smash_threshold &&
	       get_stick_x_hold_time(player, input) < plco->x_smash_frames;
}

bool check_usmash_region(const Player *player, const processed_input &input)
{
	return input.stick.y >= plco->y_smash_threshold;
}

bool check_usmash(const Player *player, const processed_input &input)
{
	return check_usmash_region(player, input) &&
	       get_stick_y_hold_time(player, input) < plco->y_smash_frames;
}

bool check_usmash_instant(const Player *player, const processed_input &input)
{
	return check_usmash(player, input) &&
	       player->input.stick.y < plco->y_smash_threshold;
}

bool check_dsmash_region(const Player *player, const processed_input &input)
{
	return -input.stick.y >= plco->y_smash_threshold;
}

bool check_dsmash(const Player *player, const processed_input &input)
{
	return check_dsmash_region(player, input) &&
	       get_stick_y_hold_time(player, input) < plco->y_smash_frames;
}

bool check_dsmash_instant(const Player *player, const processed_input &input)
{
	return check_dsmash(player, input) &&
	       -player->input.stick.y < plco->y_smash_threshold;
}

bool check_ftilt(const Player *player, const processed_input &input)
{
	return input.stick.x * player->direction >= plco->ftilt_threshold;
}

bool check_ftilt_instant(const Player *player, const processed_input &input)
{
	return check_ftilt(player, input) &&
	       player->input.stick.x * player->direction < plco->ftilt_threshold;
}

bool check_btilt(const Player *player, const processed_input &input)
{
	return -input.stick.x * player->direction >= plco->ftilt_threshold;
}

bool check_btilt_instant(const Player *player, const processed_input &input)
{
	return check_btilt(player, input) &&
	       -player->input.stick.x * player->direction < plco->ftilt_threshold;
}

bool check_utilt(const Player *player, const processed_input &input)
{
	return input.stick.y >= plco->utilt_threshold;
}

bool check_utilt_instant(const Player *player, const processed_input &input)
{
	return check_utilt(player, input) &&
	       player->input.stick.y < plco->utilt_threshold;
}

bool check_dtilt(const Player *player, const processed_input &input)
{
	return input.stick.y <= plco->dtilt_threshold;
}

bool check_dtilt_instant(const Player *player, const processed_input &input)
{
	return check_dtilt(player, input) &&
	       player->input.stick.y > plco->dtilt_threshold;
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
		if (input.pressed & Button_Z) {
			// Assume Z presses during an aerial are L cancels
			if (!player->iasa &&
			    player->action_state >= AS_AttackAirN &&
			    player->action_state <= AS_AttackAirLw) {
				return AS_None;
			}

			// Assume Z presses during jumpsquat are jc grabs
			if (player->action_state == AS_KneeBend)
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

extern const action_type turn;
extern const action_type pivot;
extern const action_type dash;
extern const action_type dashback;
extern const action_type slow_dashback;
extern const action_type run;
extern const action_type runbrake;
extern const action_type squat;
extern const action_type squat;
extern const action_type jump;
extern const action_type dj;
extern const action_type nair;
extern const action_type fair;
extern const action_type bair;
extern const action_type uair;
extern const action_type dair;
extern const action_type fsmash;
extern const action_type usmash;
extern const action_type dsmash;
extern const action_type ftilt;
extern const action_type utilt;
extern const action_type dtilt;
extern const action_type grab;
extern const action_type airdodge;
extern const action_type shine;
extern const action_type shine_turn;

bool is_ground_attack_base(const action_entry *action)
{
	return action->is_type(fsmash, usmash, dsmash,
	                       ftilt, utilt, dtilt,
	                       grab);
}

bool is_ground_base(const action_entry *action)
{
	return is_ground_attack_base(action) ||
	       action->is_type(turn, pivot, dash, dashback, slow_dashback,
	                       run, runbrake, squat, airdodge);
}

bool is_air_base(const action_entry *action)
{
	return action->is_type(nair, fair, bair, uair, dair,
	                       jump, dj);
}

bool frame_min(size_t poll_delta, size_t min)
{
	return poll_delta >= min * Si.poll.y;
}

bool frame_max(size_t poll_delta, size_t max)
{
	return poll_delta <= max * Si.poll.y;
}

bool frame_range(size_t poll_delta, size_t min, size_t max)
{
	return frame_min(poll_delta, min) && frame_max(poll_delta, max);
}

const action_type turn = {
	.name = "Turn",
	.must_succeed = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action) && !action->is_type(turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && player->action_state != AS_Dash &&
		                              player->action_state != AS_Turn;
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_btilt_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Turn && player->as_data.Turn.tilt_turn_timer > 0;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Turn;
	},
	.input_names = { nullptr }
};

const action_type pivot = {
	.name = "Pivot",
	.success_window = 2,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action) && !action->is_type(turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && player->action_state != AS_Turn;
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_bsmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Turn && player->as_data.Turn.tilt_turn_timer <= 0;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Turn && player->action_state != AS_Dash;
	},
	.input_names = { nullptr }
};

const action_type empty_pivot = {
	.name = "Empty Pivot",
	.needs_base = true,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return !action->is_type(dashback, slow_dashback);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		// Check most recent action within 2f for a pivot
		return base->is_type(pivot) && frame_max(poll_delta, 2);
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                                                 const processed_input &base_input) {
		const auto sign = std::copysign(1.f, base_input.stick.x);
		return bools_to_mask(input.stick.x * sign < plco->x_smash_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return player->action_state == AS_Turn && new_state == AS_None;
	},
	.input_names = { nullptr }
};

const action_type dash = {
	.name = "Dash",
	.success_window = 2,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && (base == nullptr || !base->is_type(run, turn, pivot));
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_fsmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Dash;
	},
	.input_names = { nullptr }
};

const action_type dashback = {
	.name = "Dash",
	.needs_base = true,
	.must_succeed = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return action->is_type(dashback, turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && !base->is_type(dashback) &&
		       frame_range(poll_delta, 1, 2);
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                                                 const processed_input &base_input) {
		const auto sign = std::copysign(1.f, base_input.stick.x);
		return bools_to_mask(input.stick.x * sign >= plco->x_smash_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Dash;
	},
	.input_names = { nullptr }
};

const action_type slow_dashback = {
	.name = "Dash",
	.needs_base = true,
	.must_succeed = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return action->is_type(slow_dashback, turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		const auto delay = (size_t)player->char_stats.tilt_turn_frames + 1;

		return is_grounded(player) && !base->is_type(slow_dashback) &&
		       frame_range(poll_delta, delay, delay + 1);
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                                                 const processed_input &base_input) {
		const auto sign = std::copysign(1.f, base_input.stick.x);
		return bools_to_mask(input.stick.x * sign >= plco->x_smash_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Dash;
	},
	.input_names = { nullptr }
};

const action_type run = {
	.name = "Run",
	.needs_base = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return true;
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return base->is_type(dash, dashback, slow_dashback) &&
		       frame_min(poll_delta, get_initial_dash(player)) &&
		       player->action_state == AS_Dash;
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.stick.x * player->direction >= plco->run_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Run;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_Run;
	},
	.input_names = { nullptr }
};

const action_type runbrake = {
	.name = "Run Brake",
	.needs_base = true,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return action->is_type(run);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(
			input.stick.x         * player->direction <  plco->run_threshold &&
			player->input.stick.x * player->direction >= plco->run_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_RunBrake;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != AS_RunBrake;
	},
	.input_names = { nullptr }
};

const action_type squat = {
	.name = "Crouch",
	.needs_base = true,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return action->is_type(runbrake, squat);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return !base->is_type(squat) && frame_min(poll_delta, 1);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.stick.y < -plco->squat_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Squat;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state < AS_Squat || player->action_state > AS_SquatRv;
	},
	.input_names = { nullptr }
};

const action_type jump = {
	.name = "Jump",
	.success_window = 4,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action) || action->is_type(shine, shine_turn);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_usmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_KneeBend;
	},
	.end_predicate = [](const Player *player) {
		return !is_airborne(player);
	},
	.input_names = { "X", "Y", "Up" }
};

#warning TODO: multijumps
const action_type dj = {
	.name = "DJ",
	.success_window = 4,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_air_base(action) || action->is_type(shine, shine_turn);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_usmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
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
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		if constexpr (state == AS_AttackAirHi) {
			if (action->is_type(usmash))
				return true;
		}

		return is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		if constexpr (state == AS_AttackAirHi) {
			if (base != nullptr) {
				if (base->is_type(usmash))
					return false;

				// Assume jc usmash if guaranteed to come out during jumpsquat
				const auto jumpsquat = (size_t)player->char_stats.jumpsquat;

				if (base->is_type(jump) && frame_max(poll_delta, jumpsquat - 1))
						return false;
			}
		}
		return is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_aerial(player, input) == state);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == state;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state != state;
	},
	.input_names = { nullptr }
};

const action_type nair = aerial<"Nair", AS_AttackAirN>;
const action_type fair = aerial<"Fair", AS_AttackAirF>;
const action_type bair = aerial<"Bair", AS_AttackAirB>;
const action_type uair = aerial<"Uair", AS_AttackAirHi>;
const action_type dair = aerial<"Dair", AS_AttackAirLw>;

const action_type fsmash = {
	.name = "FSmash",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		const auto stick  = check_xsmash(player, input) && (input.pressed & Button_A);
		const auto cstick = std::abs(input.cstick.x)         >= plco->x_smash_threshold &&
		                    std::abs(player->input.cstick.x) <  plco->x_smash_threshold;

		return bools_to_mask(stick, cstick);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state >= AS_AttackS4Hi && new_state <= AS_AttackS4Lw;
	},
	.end_predicate = [](const Player *player) {
		return player->iasa || player->action_state < AS_AttackS4Hi
		                    || player->action_state > AS_AttackS4Lw;
	},
	.input_names = { nullptr, nullptr }
};

const action_type usmash = {
	.name = "USmash",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action) || action->is_type(jump);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) || (base != nullptr && base->is_type(jump));
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		const auto stick  = check_usmash(player, input) && (input.pressed & Button_A);
		const auto cstick = input.cstick.y         >= plco->usmash_threshold &&
		                    player->input.cstick.y <  plco->usmash_threshold;

		return bools_to_mask(stick, cstick);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_AttackHi4;
	},
	.end_predicate = [](const Player *player) {
		return player->iasa || player->action_state != AS_AttackHi4;
	},
	.input_names = { nullptr, nullptr }
};

const action_type dsmash = {
	.name = "DSmash",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		const auto stick  = check_dsmash(player, input) && (input.pressed & Button_A);
		const auto cstick = input.cstick.y         <= plco->dsmash_threshold &&
		                    player->input.cstick.y >  plco->dsmash_threshold;

		return bools_to_mask(stick, cstick);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_AttackLw4;
	},
	.end_predicate = [](const Player *player) {
		return player->iasa || player->action_state != AS_AttackLw4;
	},
	.input_names = { nullptr, nullptr }
};

const action_type ftilt = {
	.name = "FTilt",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask((input.pressed & Button_A) &&
		                     check_ftilt(player, input)
		                     && !check_xsmash(player, input) &&
		                     get_stick_angle(input.stick) < plco->angle_50d);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state >= AS_AttackS3Hi && new_state <= AS_AttackS3Lw;
	},
	.end_predicate = [](const Player *player) {
		return player->iasa || player->action_state < AS_AttackS3Hi
		                    || player->action_state > AS_AttackS3Lw;
	},
	.input_names = { nullptr }
};

const action_type utilt = {
	.name = "UTilt",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask((input.pressed & Button_A) &&
		                     check_utilt(player, input) &&
		                     !check_usmash(player, input) &&
		                     get_stick_angle(input.stick) >= plco->angle_50d);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_AttackHi3;
	},
	.end_predicate = [](const Player *player) {
		return player->iasa || player->action_state != AS_AttackHi3;
	},
	.input_names = { nullptr }
};

const action_type dtilt = {
	.name = "DTilt",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask((input.pressed & Button_A) &&
		                     check_dtilt(player, input) &&
		                     !check_dsmash(player, input) &&
		                     get_stick_angle(input.stick) >= plco->angle_50d);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_AttackLw3;
	},
	.end_predicate = [](const Player *player) {
		return player->iasa || player->action_state != AS_AttackLw3;
	},
	.input_names = { nullptr }
};

const action_type grab = {
	.name = "Grab",
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action) || action->is_type(jump);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) || player->action_state == AS_KneeBend;
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(all_set(input.pressed, Button_AnalogLR | Button_A));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Catch || new_state == AS_CatchDash;
	},
	.end_predicate = [](const Player *player) {
		return player->action_state < AS_Catch || player->action_state > AS_CatchCut;
	},
	.input_names = { nullptr }
};

const action_type airdodge = {
	.name = "Air Dodge",
	.plinkable = true,
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return (base != nullptr && base->is_type(jump)) || is_airborne(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_L,
		                     input.pressed & Button_R);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_EscapeAir;
	},
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_EscapeAir) &&
		       !in_state(player, AS_LandingFallSpecial);
	},
	.input_names = { "L", "R" }
};

const action_type shine = {
	.name = "Shine",
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		return is_ground_base(action) || is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_char(player, CID_Fox, CID_Falco) &&
		       (is_grounded(player) || is_airborne(player));
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_down_b(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Fox_SpecialLwStart ||
		       new_state == AS_Fox_SpecialAirLwStart;
	},
	.end_predicate = [](const Player *player) {
		return !in_state_range(player, AS_Fox_SpecialLwStart, AS_Fox_SpecialAirLwTurn);
	},
	.input_names = { nullptr }
};

const action_type shine_turn = {
	.name = "Shine Turn",
	.needs_base = true,
	.is_base_action = [](const action_entry *action, size_t poll_delta) {
		// Display shine turns less than 1f after a jump input because turn takes priority
		return action->is_type(shine, shine_turn) ||
		       (action->is_type(jump) && frame_min(poll_delta, 1));
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_char(player, CID_Fox, CID_Falco) &&
		       !in_state(player, AS_Fox_SpecialLwEnd) &&
		       !in_state(player, AS_Fox_SpecialAirLwEnd) &&
		       base->is_type(shine, shine_turn) &&
		       frame_min(poll_delta, 3);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_btilt(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Fox_SpecialLwTurn ||
		       new_state == AS_Fox_SpecialAirLwTurn;
	},
	.end_predicate = [](const Player *player) {
		return !in_state_range(player, AS_Fox_SpecialLwStart, AS_Fox_SpecialAirLwTurn);
	},
	.input_names = { nullptr }
};

} // action_type_definitions

static const action_type *action_types[] = {
	&action_type_definitions::turn,
	&action_type_definitions::pivot,
	&action_type_definitions::empty_pivot,
	&action_type_definitions::run,
	&action_type_definitions::runbrake,
	&action_type_definitions::squat,
	&action_type_definitions::dash,
	&action_type_definitions::dashback,
	&action_type_definitions::slow_dashback,
	&action_type_definitions::jump,
	&action_type_definitions::dj,
	&action_type_definitions::nair,
	&action_type_definitions::fair,
	&action_type_definitions::bair,
	&action_type_definitions::uair,
	&action_type_definitions::dair,
	&action_type_definitions::fsmash,
	&action_type_definitions::usmash,
	&action_type_definitions::dsmash,
	&action_type_definitions::ftilt,
	&action_type_definitions::utilt,
	&action_type_definitions::dtilt,
	&action_type_definitions::grab,
	&action_type_definitions::airdodge,
	&action_type_definitions::shine,
	&action_type_definitions::shine_turn
};

constexpr auto action_type_count = std::extent_v<decltype(action_types)>;
static ring_buffer<saved_input, INPUT_BUFFER_SIZE> input_buffer[4];
static ring_buffer<action_entry, ACTION_BUFFER_SIZE> action_buffer;
static unsigned int draw_frames;

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat == 0)
		input_buffer[chan].add({ .qwrite = HSD_PadLibData.qwrite, .status = status });
});

static void detect_action_for_input(const Player *player, const processed_input &input,
                                    size_t poll_index, size_t type_index, u32 *detected_inputs)
{
	const auto &type = *action_types[type_index];

	const action_entry *base = nullptr;
	auto plinked = false;

	// Find base action in buffer
	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		const auto *action = action_buffer.head(offset);
		const auto poll_delta = poll_index - action->poll_index;

		if (type.plinkable && !plinked && action->is_type(type)) {
			// Check if this is the 2nd input in a plink
			plinked = poll_delta <= PLINK_WINDOW * Si.poll.y;
			continue;
		}

		// Don't use previous inputs of a plinked action as a base
		if (plinked && action->is_type(type))
			continue;

		if (base == nullptr && type.is_base_action != nullptr) {
			// Count the 2nd input in a plink even if the base action ended
			if (type.is_base_action(action, poll_delta) && (action->active || plinked))
				base = action;
		}
	}

	// Check if type requires base action
	if (base == nullptr && type.needs_base)
		return;

	// Check action prerequisites unless plinked
	if (!plinked && type.state_predicate != nullptr) {
		const auto poll_delta = base != nullptr ? poll_index - base->poll_index : 0;
		if (!type.state_predicate(player, base, poll_delta))
			return;
	}

	// Don't detect the same inputs for the same action repeatedly in one frame
	auto mask = 0;

	if (type.input_predicate != nullptr)
		mask |= type.input_predicate(player, input);

	if (type.base_input_predicate != nullptr && base != nullptr)
		mask |= type.base_input_predicate(player, input, base->input);

	// Don't detect the same inputs for the same action repeatedly in one frame
	mask &= ~detected_inputs[type_index];

	if (mask == 0)
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
			.input       = input,
			.frame       = draw_frames,
			.active      = !type.must_succeed,
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

EVENT_HANDLER(events::player::think::input::post, [](Player *player, u32 old_state, u32 new_state)
{
	if (Player_IsCPU(player))
		return;

	const auto port = player->port;
	auto performed_action = false;

	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		auto *action = action_buffer.tail(offset);
		if (action->port != port)
			continue;

		const auto *type = action->type;

		if (!action->confirmed) {
			// Figure out which actions succeeded
			if (!performed_action && type->success_predicate(player, new_state)) {
				action->active = true;
				action->success = true;
				action->confirmed = true;
				performed_action = true;
			} else if (++action->success_timer >= type->success_window) {
				action->active = !type->must_succeed;
				action->confirmed = true;
			}
		} else if (action->active && type->end_predicate != nullptr) {
			// Check if action can still be used as base
			if (action->end_timer == 0) {
				if (type->end_predicate(player))
					action->end_timer = type->end_delay + 1;
			} else if (--action->end_timer == 0) {
				action->active = false;
			}
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

	const auto max_count = action_buffer.stored();
	size_t displayed = 0;

	for (size_t offset = 0; offset < max_count && displayed < ACTION_HISTORY; offset++) {
		const auto *action = action_buffer.head(offset);
                const auto *base_action = action->base_action;
                const auto *input_name = action->type->input_names[action->input_type];

		if (action->type->must_succeed && !action->success)
			continue;
		else
			displayed++;

		// Check if base action got pushed out of the buffer
		if (base_action != nullptr && base_action->poll_index > action->poll_index)
			break;

                ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (action->success)
			ImGui::TextColored({.2f, 1.f, .2f, 1.f}, "✔");
		else
			ImGui::TextColored({1.f, .2f, .2f, 1.f}, "❌");

		ImGui::SameLine();

		if (base_action != nullptr) {
			const auto poll_delta = action->poll_index - base_action->poll_index;
			const auto frame_delta = (float)poll_delta / Si.poll.y;

			if (frame_delta < 100)
				ImGui::Text("%5.2ff", frame_delta);
			else
				ImGui::TextUnformatted("   ...");
		} else {
			ImGui::TextUnformatted("      ");
		}

		ImGui::TableNextColumn();

		if (base_action != nullptr)
			ImGui::Text("%s -> %s", base_action->type->name, action->type->name);
		else
			ImGui::TextUnformatted(action->type->name);

		if (input_name != nullptr) {
			ImGui::SameLine();
			ImGui::Text("(%s)", input_name);
		}

		if (base_action == nullptr) {
			// Spacer row
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