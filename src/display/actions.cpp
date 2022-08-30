#include "dolphin/os.h"
#include "dolphin/serial.h"
#include "dolphin/vi.h"
#include "hsd/pad.h"
#include "melee/action_state.h"
#include "melee/constants.h"
#include "melee/player.h"
#include "melee/subaction.h"
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
#include "util/melee/character.h"
#include "util/melee/ftcmd.h"
#include "util/melee/pad.h"
#include <bit>
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

struct action_entry;

struct action_type {
	using printer = void(const char *fmt, ...);

	const char *name;
	// If true, can only happen if a base action is found
	bool needs_base;
	// If true, ignore state/base action end if last action input is within PLINK_WINDOW frames
	bool plinkable;
	// If true, action must succeed to be active or displayed
	bool must_succeed;
	// If true, action will be functional, but not be displayed
	bool hidden;
	// How many frames to continue checking for action success for
	unsigned int success_window = 1;
	// How many frames to still consider action active after end_predicate returns true
	unsigned int end_delay;
	// Automatically add this action when this state is entered
	u32 on_action_state = AS_None;
	// Check whether a previous action is a suitable base action (relative timing to) for this
	bool(*is_base_action)(const action_entry *action);
	// Predicate to detect prerequisite player state for this action (before PlayerThink_Input)
	bool(*state_predicate)(const Player *player, const action_entry *base,
	                                             size_t poll_delta);
	// Predicate to detect inputs that trigger this action (before PlayerThink_Input)
	// Returns 0 or an arbitrary mask corresponding to the input method (e.g. X/Y/Up for jump)
	int(*input_predicate)(const Player *player, const processed_input &input);
	// Like input_predicate, but receives base action.
	int(*base_input_predicate)(const Player *player, const processed_input &input,
	                           const action_entry *base);
	// Predicate to detect whether the action succeeded (after PlayerThink_Input)
	// new_state is AS_None if there was no AS change
	bool(*success_predicate)(const Player *player, s32 new_state);
	// Predicate to detect when this can no longer be used as a valid base action
	bool(*end_predicate)(const Player *player);
	// Show a custom formatted string instead of input_names
	void(*format_description)(const action_entry *action, printer *printer);
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
	// Final input from the frame this action was performed on
	PlayerInput final_input;
	// Player direction from frame this action was performed on
	float direction;
	// Set when above two fields are initialized
	bool final_input_set;
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

extern const action_type turn;
extern const action_type pivot;
extern const action_type dash;
extern const action_type dashback;
extern const action_type slow_dashback;
extern const action_type run;
extern const action_type runbrake;
extern const action_type squat;
extern const action_type squatwait;
extern const action_type squatrv;
extern const action_type dooc_start;
extern const action_type jump;
extern const action_type dj;
extern const action_type multijump;
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
extern const action_type side_b;
extern const action_type up_b;
extern const action_type down_b;
extern const action_type neutral_b;
extern const action_type cliffcatch;
extern const action_type cliffwait;
extern const action_type ledgeattack;
extern const action_type ledgeroll;
extern const action_type ledgejump;
extern const action_type ledgestand;
extern const action_type ledgefall;

enum class state_type {
	ground,
	air,
	ledge,
	knockdown
};

bool in_state(const Player *player, auto...states)
{
	return ((player->action_state == states) || ...);
}

bool in_state_range(const Player *player, s32 start, s32 end)
{
	return player->action_state >= start && player->action_state <= end;
}

bool is_char(const Player *player, auto ...characters)
{
	return ((player->character_id == characters) || ...);
}

bool is_multijump_state(const Player *player, s32 state)
{
	if (!player->multijump)
		return false;

	const auto *stats = player->extra_stats.multijump_stats;
	const auto start1 = stats->start_state;
	const auto start2 = stats->start_state_helmet;

	return (start1 != -1 && state >= start1 && state < start1 + stats->state_count) ||
	       (start2 != -1 && state >= start2 && state < start2 + stats->state_count);
}

bool in_multijump_state(const Player *player)
{
	return is_multijump_state(player, player->action_state);
}

state_type get_state_type(const Player *player, const action_entry *base)
{
	if (base != nullptr && base->is_type(jump, ledgefall))
		return state_type::air;

	if (in_state_range(player, AS_CliffCatch, AS_CliffJumpQuick2))
		return state_type::ledge;

	if (in_state_range(player, AS_DownBoundU, AS_DownDamageU))
		return state_type::knockdown;

	if (in_state_range(player, AS_DownBoundD, AS_DownDamageD))
		return state_type::knockdown;

	// Consider inputs in jumpsquat to be attempted airborne inputs
	if (player->airborne || in_state(player, AS_KneeBend))
		return state_type::air;

	return state_type::ground;
}

bool is_on_ledge(const Player *player, const action_entry *base = nullptr)
{
	return get_state_type(player, base) == state_type::ledge;
}
bool is_grounded(const Player *player, const action_entry *base = nullptr)
{
	return get_state_type(player, base) == state_type::ground;
}
bool is_airborne(const Player *player, const action_entry *base = nullptr)
{
	return get_state_type(player, base) == state_type::air;
}

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

bool check_dfooc(const Player *player, const processed_input &input)
{
	// Use 3f window for 1.03 + displaying slow inputs on UCF
	return check_fsmash_region(player, input) &&
	       get_stick_x_hold_time(player, input) < 3;
}

bool check_dfooc_instant(const Player *player, const processed_input &input)
{
	return check_dfooc(player, input) &&
	       -player->input.stick.x * player->direction < plco->x_smash_threshold;
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

bool check_dbooc(const Player *player, const processed_input &input)
{
	// Use 3f window for 1.03 + displaying slow inputs on UCF
	return check_bsmash_region(player, input) &&
	       get_stick_x_hold_time(player, input) < 3;
}

bool check_dbooc_instant(const Player *player, const processed_input &input)
{
	return check_dbooc(player, input) &&
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

special_type check_special(const Player *player, const processed_input &input,
                           const action_entry *base)
{
	if (!(input.pressed & Button_B))
		return special_type::none;

	const auto crouch = in_state(player, AS_SquatWait, AS_SquatRv);

	if (is_grounded(player, base)) {
		if (std::abs(input.stick.x) >= plco->x_special_threshold && !crouch)
			return special_type::side;
		if (input.stick.y >= plco->y_special_threshold)
			return special_type::up;
		if (input.stick.y < -plco->y_special_threshold)
			return special_type::down;
		if (input.stick.y > -plco->y_special_threshold && !crouch)
			return special_type::neutral;
	} else if (is_airborne(player, base)) {
		if (input.stick.y >= plco->y_special_threshold)
			return special_type::up;
		if (input.stick.y <= -plco->y_special_threshold)
			return special_type::down;
		if (std::abs(input.stick.x) >= plco->x_special_threshold)
			return special_type::side;

		return special_type::neutral;
	}

	return special_type::none;
}

int check_aerial(const Player *player, const processed_input &input)
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

bool check_ledge_deadzone(const vec2 &stick)
{
	return std::abs(stick.x) < plco->ledge_deadzone &&
	       std::abs(stick.y) < plco->ledge_deadzone;
}

bool check_ledge_deadzone_frame(const Player *player)
{
	return check_ledge_deadzone(player->input.stick) &&
	       check_ledge_deadzone(player->input.cstick);
}

bool check_ledgefall(const Player *player, const vec2 &stick)
{
	if (check_ledge_deadzone(stick))
		return false;
	if (stick.x * player->direction >= 0)
		return get_stick_angle_abs_x(stick) < -plco->angle_50d;
	else
		return get_stick_angle_abs_x(stick) < plco->angle_50d;
}

bool check_ledgestand(const Player *player, const vec2 &stick)
{
	if (check_ledge_deadzone(stick))
		return false;
	if (stick.x * player->direction >= 0)
		return get_stick_angle_abs_x(stick) >= -plco->angle_50d;
	else
		return get_stick_angle_abs_x(stick) >= plco->angle_50d;
}

bool is_ground_base(const action_entry *action)
{
	return action->is_type(turn, pivot, dash, dashback, slow_dashback, run, runbrake, squat,
	                       airdodge,
	                       fsmash, usmash, dsmash,
	                       ftilt, utilt, dtilt,
	                       grab,
	                       side_b, up_b, down_b, neutral_b);
}

bool is_air_base(const action_entry *action)
{
	return action->is_type(nair, fair, bair, uair, dair,
	                       jump, dj, multijump,
	                       side_b, up_b, down_b, neutral_b,
	                       ledgefall);
}

bool is_ledge_base(const action_entry *action)
{
	return action->is_type(cliffcatch, cliffwait, ledgefall);
}

bool frame_min(size_t poll_delta, auto min)
{
	return poll_delta >= (size_t)(min * Si.poll.y);
}

bool frame_max(size_t poll_delta, auto max)
{
	return poll_delta <= (size_t)(max * Si.poll.y);
}

bool frame_range(size_t poll_delta, auto min, auto max)
{
	return frame_min(poll_delta, min) && frame_max(poll_delta, max);
}

void format_coord(float coord, action_type::printer *printer) {
	if (coord == 0.f)
		printer("  0.0");
	else if (coord == 1.f)
		printer("  1.0");
	else if (coord == -1.f)
		printer(" -1.0");
	else
		printer("%5.0f", coord * 10000);
}

const action_type turn = {
	.name = "Turn",
	.must_succeed = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action) {
		return is_ground_base(action) && !action->is_type(turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && !in_state(player, AS_Dash, AS_Turn) &&
		                              !in_state_range(player, AS_Squat, AS_SquatRv);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_btilt_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Turn && player->as_data.Turn.tilt_turn_timer > 0;
	},
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_Turn);
	},
	.input_names = { nullptr }
};

const action_type pivot = {
	.name = "Pivot",
	.success_window = 3,
	.is_base_action = [](const action_entry *action) {
		return (is_ground_base(action) && !action->is_type(turn, pivot)) ||
		       action->is_type(squatrv, dooc_start);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && !in_state(player, AS_Turn);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		if (in_state_range(player, AS_Squat, AS_SquatRv))
			return bools_to_mask(check_dbooc_instant(player, input));
		else
			return bools_to_mask(check_bsmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Turn && player->as_data.Turn.tilt_turn_timer <= 0;
	},
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_Turn, AS_Dash);
	},
	.input_names = { nullptr }
};

const action_type empty_pivot = {
	.name = "Empty Pivot",
	.needs_base = true,
	.is_base_action = [](const action_entry *action) {
		return !action->is_type(dashback, slow_dashback);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		// Check most recent action within 2f for a pivot
		return base->is_type(pivot) && frame_max(poll_delta, 2);
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                           const action_entry *base) {
		const auto sign = std::copysign(1.f, base->input.stick.x);
		return bools_to_mask(input.stick.x * sign < plco->x_smash_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return player->action_state == AS_Turn && new_state == AS_None;
	},
	.input_names = { nullptr }
};

const action_type dash = {
	.name = "Dash",
	.success_window = 3,
	.is_base_action = [](const action_entry *action) {
		return is_ground_base(action) || action->is_type(squatrv, dooc_start);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && (base == nullptr || !base->is_type(run, turn, pivot));
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		if (in_state_range(player, AS_Squat, AS_SquatRv))
			return bools_to_mask(check_dfooc_instant(player, input));
		else
			return bools_to_mask(check_fsmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_Dash);
	},
	.input_names = { nullptr }
};

const action_type dashback = {
	.name = "Dash",
	.needs_base = true,
	.must_succeed = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action) {
		return action->is_type(dashback, turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_grounded(player) && !base->is_type(dashback) &&
		       frame_range(poll_delta, 1, 2);
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                           const action_entry *base) {
		const auto sign = std::copysign(1.f, base->input.stick.x);
		return bools_to_mask(input.stick.x * sign >= plco->x_smash_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_Dash);
	},
	.input_names = { nullptr }
};

const action_type slow_dashback = {
	.name = "Dash",
	.needs_base = true,
	.must_succeed = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action) {
		return action->is_type(slow_dashback, turn, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		const auto delay = player->char_stats.tilt_turn_frames + 1;

		return is_grounded(player) && !base->is_type(slow_dashback) &&
		       frame_range(poll_delta, delay, delay + 1);
	},
	.base_input_predicate = [](const Player *player,     const processed_input &input,
	                           const action_entry *base) {
		const auto sign = std::copysign(1.f, base->input.stick.x);
		return bools_to_mask(input.stick.x * sign >= plco->x_smash_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_Dash;
	},
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_Dash);
	},
	.input_names = { nullptr }
};

const action_type run = {
	.name = "Run",
	.needs_base = true,
	.success_window = 2,
	.is_base_action = [](const action_entry *action) {
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
		return !in_state(player, AS_Run);
	},
	.input_names = { nullptr }
};

const action_type runbrake = {
	.name = "Run Brake",
	.needs_base = true,
	.is_base_action = [](const action_entry *action) {
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
		return !in_state(player, AS_RunBrake);
	},
	.input_names = { nullptr }
};

const action_type squat = {
	.name = "Crouch",
	.needs_base = true,
	.is_base_action = [](const action_entry *action) {
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
		return !in_state_range(player, AS_Squat, AS_SquatRv);
	},
	.input_names = { nullptr }
};

const action_type squatwait = {
	.name = "Full Crouch",
	.is_base_action = [](const action_entry *action) {
		return action->is_type(squat, squatwait, dooc_start);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		if (base == nullptr)
			return in_state(player, AS_SquatWait);
		else if (base->is_type(squatwait, dooc_start))
			return false;
		else
			return frame_min(poll_delta, get_subaction_length(player, SA_Squat) - 1);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.stick.y <= -plco->max_squatwait_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return in_state(player, AS_SquatWait);
	},
	.end_predicate = [](const Player *player) {
		return !in_state_range(player, AS_Squat, AS_SquatRv);
	},
	.input_names = { nullptr }
};

const action_type squatrv = {
	.name = "Uncrouch",
	.needs_base = true,
	.is_base_action = [](const action_entry *action) {
		return action->is_type(squatwait, dooc_start, squatrv, dash, dashback, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return in_state_range(player, AS_Squat, AS_SquatRv) &&
		       !base->is_type(squatrv, dash, dashback, pivot);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.stick.y > -plco->max_squatwait_threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return new_state == AS_SquatRv;
	},
	.end_predicate = [](const Player *player) {
		return !in_state_range(player, AS_Squat, AS_SquatRv);
	},
	.input_names = { nullptr }
};

const action_type dooc_start = {
	.name = "DOOC Start",
	.hidden = true,
	.is_base_action = [](const action_entry *action) {
		return action->is_type(dooc_start, squatrv, dash, pivot);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return in_state(player, AS_Squat, AS_SquatWait) &&
		       (base == nullptr || !base->is_type(squatrv, dash, pivot));
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                           const action_entry *base) {
		if (get_stick_x_hold_time(player, input) >= 3)
			return 0;
		else if (base != nullptr)
			return bools_to_mask(input.stick.x * base->input.stick.x < 0);
		else
			return bools_to_mask(input.stick.x != 0);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return false;
	},
	.end_predicate = [](const Player *player) {
		return !in_state_range(player, AS_Squat, AS_SquatRv) ||
		       player->input.stick_x_hold_time >= 3;
	},
	.input_names = { nullptr }
};

const action_type jump = {
	.name = "Jump",
	.success_window = 4,
	.is_base_action = [](const action_entry *action) {
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

const action_type dj = {
	.name = "DJ",
	.success_window = 4,
	.is_base_action = [](const action_entry *action) {
		return is_air_base(action) || action->is_type(shine, shine_turn);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_airborne(player, base) && !in_multijump_state(player) &&
		       player->jumps_used < player->char_stats.jumps;
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_usmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		if (player->multijump)
			return is_multijump_state(player, new_state);
		else
			return new_state == AS_JumpAerialF || new_state == AS_JumpAerialB;
	},
	.end_predicate = [](const Player *player) {
		return !is_airborne(player);
	},
	.format_description = [](const action_entry *action, action_type::printer *printer) {
		const auto *input = (const char*[]) {
			"X", "Y", "Up"
		}[action->input_type];

		// Display jump trajectory during ledgedashes
		if (action->base_action != nullptr && action->base_action->is_type(ledgefall)) {
			format_coord(action->final_input.stick.x * action->direction, printer);
			printer(" %s", input);
		} else {
			printer("%s", input);
		}
	}
};

const action_type multijump = {
	.name = "DJ",
	.needs_base = true,
	.success_window = 4,
	.is_base_action = [](const action_entry *action) {
		return true;
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		// Check for buffering repeated djs
		return in_multijump_state(player) &&
		       player->jumps_used < player->char_stats.jumps &&
		       base->is_type(dj, multijump) &&
		       frame_min(poll_delta, get_multijump_cooldown(player));
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.buttons & Button_X,
		                     input.buttons & Button_Y,
		                     check_usmash_region(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return is_multijump_state(player, new_state);
	},
	.end_predicate = [](const Player *player) {
		return !is_airborne(player);
	},
	.input_names = { "X", "Y", "Up" }
};

template<string_literal name, s32 state>
const action_type aerial = {
	.name = name.value,
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action) {
		if (state == AS_AttackAirHi && action->is_type(usmash))
				return true;

		return is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		if (state == AS_AttackAirHi && base != nullptr) {
			if (base->is_type(usmash))
				return false;

			// Assume jc usmash if guaranteed to come out during jumpsquat
			if (base->is_type(jump)) {
				const auto jumpsquat = player->char_stats.jumpsquat;
				if (frame_max(poll_delta, jumpsquat - 1))
						return false;
			}
		}
		return is_airborne(player, base);
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
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
	.is_base_action = [](const action_entry *action) {
		return is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_airborne(player, base);
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
	.format_description = [](const action_entry *action, action_type::printer *printer) {
		if (action->final_input.stick != vec2::zero)
			printer("%4.1f ", rad_to_deg(get_stick_angle(action->final_input.stick)));

		printer("%s", action->input_type == 0 ? "L" : "R");
	}
};

const action_type shine = {
	.name = "Shine",
	.is_base_action = [](const action_entry *action) {
		return is_ground_base(action) || is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_char(player, CID_Fox, CID_Falco) &&
		       (is_grounded(player) || is_airborne(player, base));
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                                                 const action_entry *base) {
		return bools_to_mask(check_special(player, input, base) == special_type::down);
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
	.is_base_action = [](const action_entry *action) {
		// Display shine turns less than 1f after a jump input because turn takes priority
		return action->is_type(shine, shine_turn);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_char(player, CID_Fox, CID_Falco) &&
		       !in_state(player, AS_Fox_SpecialLwEnd) &&
		       !in_state(player, AS_Fox_SpecialAirLwEnd) &&
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

template<string_literal name, special_type type>
const action_type special = {
	.name = name.value,
	.end_delay = ACT_OUT_WINDOW,
	.is_base_action = [](const action_entry *action) {
		return is_ground_base(action) || is_air_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		if (type == special_type::down && is_char(player, CID_Fox, CID_Falco))
			return false;

		return is_grounded(player, base) || is_airborne(player, base);
	},
	.base_input_predicate = [](const Player *player, const processed_input &input,
	                                                 const action_entry *base) {
		return bools_to_mask(check_special(player, input, base) == type);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return check_special_state(player, new_state) == type;
	},
	.end_predicate = [](const Player *player) {
		return check_special_state(player, player->action_state) != type;
	},
	.input_names = { nullptr }
};

const action_type side_b    = special<"Side B",    special_type::side>;
const action_type up_b      = special<"Up B",      special_type::up>;
const action_type down_b    = special<"Down B",    special_type::down>;
const action_type neutral_b = special<"Neutral B", special_type::neutral>;

const action_type cliffcatch = {
	.name = "Cliff Catch",
	.on_action_state = AS_CliffCatch,
	.end_predicate = [](const Player *player) {
		return !in_state(player, AS_CliffCatch);
	},
	.input_names = { nullptr }
};

const action_type cliffwait = {
	.name = "Cliff Wait",
	.needs_base = true,
	.is_base_action = [](const action_entry *action) {
		return action->is_type(cliffcatch, cliffwait);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return !base->is_type(cliffwait) && in_state(player, AS_CliffWait);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(true);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return true;
	},
	.end_predicate = [](const Player *player) {
		return !is_on_ledge(player);
	},
	.input_names = { nullptr }
};

const action_type ledgeattack = {
	.name = "Ledge Attack",
	.is_base_action = [](const action_entry *action) {
		return is_ledge_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_on_ledge(player, base);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		const auto threshold = plco->ledge_attack_cstick_threshold;
		const auto cstick_y = input.cstick.y;
		const auto last_cstick_y = player->input.cstick.y;
		return bools_to_mask(input.pressed & Button_A,
		                     input.pressed & Button_B,
		                     cstick_y >= threshold && last_cstick_y < threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return in_state(player, AS_CliffAttackSlow, AS_CliffAttackQuick);
	},
	.input_names = { nullptr, nullptr, nullptr }
};

const action_type ledgeroll = {
	.name = "Ledge Roll",
	.is_base_action = [](const action_entry *action) {
		return is_ledge_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_on_ledge(player, base);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		const auto threshold = plco->ledge_roll_cstick_threshold;
		const auto cstick_x = input.cstick.x * player->direction;
		const auto last_cstick_x = player->input.cstick.x * player->direction;
		return bools_to_mask(input.pressed & Button_AnalogLR,
		                     cstick_x >= threshold && last_cstick_x < threshold);
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return in_state(player, AS_CliffEscapeSlow, AS_CliffEscapeQuick);
	},
	.input_names = { nullptr, nullptr }
};

const action_type ledgejump = {
	.name = "Ledge Jump",
	.is_base_action = [](const action_entry *action) {
		return is_ledge_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_on_ledge(player, base);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(input.pressed & Button_X,
		                     input.pressed & Button_Y,
		                     check_usmash_instant(player, input));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return in_state_range(player, AS_CliffJumpSlow1, AS_CliffJumpQuick2);
	},
	.input_names = { nullptr, nullptr, nullptr }
};

const action_type ledgestand = {
	.name = "Ledge Stand",
	.is_base_action = [](const action_entry *action) {
		return is_ledge_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_on_ledge(player, base) && check_ledge_deadzone_frame(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_ledgestand(player, input.stick),
		                     check_ledgestand(player, input.cstick));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return in_state(player, AS_CliffClimbSlow, AS_CliffClimbQuick);
	},
	.input_names = { nullptr, nullptr }
};

const action_type ledgefall = {
	.name = "Ledge Fall",
	.is_base_action = [](const action_entry *action) {
		return is_ledge_base(action);
	},
	.state_predicate = [](const Player *player, const action_entry *base, size_t poll_delta) {
		return is_on_ledge(player, base) && check_ledge_deadzone_frame(player);
	},
	.input_predicate = [](const Player *player, const processed_input &input) {
		return bools_to_mask(check_ledgefall(player, input.stick),
		                     check_ledgefall(player, input.cstick));
	},
	.success_predicate = [](const Player *player, s32 new_state) {
		return in_state(player, AS_Fall);
	},
	.end_predicate = [](const Player *player) {
		// Show ledgedash sequence against ledgefall eaten by ledge jump
		return !is_airborne(player) &&
		       !in_state_range(player, AS_CliffJumpSlow1, AS_CliffJumpQuick2);
	},
	.input_names = { nullptr, nullptr }
};

} // action_type_definitions

static const action_type *action_types[] = {
	&action_type_definitions::dooc_start,
	&action_type_definitions::turn,
	&action_type_definitions::pivot,
	&action_type_definitions::empty_pivot,
	&action_type_definitions::dash,
	&action_type_definitions::dashback,
	&action_type_definitions::slow_dashback,
	&action_type_definitions::run,
	&action_type_definitions::runbrake,
	&action_type_definitions::squat,
	&action_type_definitions::squatwait,
	&action_type_definitions::squatrv,
	&action_type_definitions::dj,
	&action_type_definitions::multijump,
	&action_type_definitions::jump,
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
	&action_type_definitions::shine_turn,
	&action_type_definitions::neutral_b,
	&action_type_definitions::side_b,
	&action_type_definitions::up_b,
	&action_type_definitions::down_b,
	&action_type_definitions::cliffcatch,
	&action_type_definitions::cliffwait,
	&action_type_definitions::ledgeattack,
	&action_type_definitions::ledgeroll,
	&action_type_definitions::ledgejump,
	&action_type_definitions::ledgestand,
	&action_type_definitions::ledgefall,
};

constexpr auto action_type_count = std::extent_v<decltype(action_types)>;
static ring_buffer<saved_input, INPUT_BUFFER_SIZE> input_buffer[4];
static ring_buffer<action_entry, ACTION_BUFFER_SIZE> action_buffer;
static size_t last_action_poll[action_type_count];

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat == 0)
		input_buffer[chan].add({ .qwrite = HSD_PadLibData.qwrite, .status = status });
});

struct action_detect_data {
	u32 detected_inputs;
	size_t bases_checked;
	const action_entry *base;
};

static void detect_action_for_input(const Player *player, const processed_input &input,
                                    size_t poll_index, size_t type_index, action_detect_data *data)
{
	const auto &type = *action_types[type_index];

	const action_entry *base = data->base;

	const auto plink_delta = poll_index - last_action_poll[type_index];
	const auto plinked = type.plinkable && plink_delta <= PLINK_WINDOW * Si.poll.y;

	// Find base action in buffer
	if (type.is_base_action != nullptr) {
		const auto unchecked = action_buffer.count() - data->bases_checked;
		const auto max_offset = std::min(unchecked, action_buffer.stored());

		for (size_t offset = 0; offset < max_offset; offset++) {
			const auto *action = action_buffer.head(offset);

			// Don't use previous inputs of a plinked action as a base
			if (plinked && action->is_type(type))
				continue;

			// Count the 2nd input in a plink even if the base action ended
			if ((action->active || plinked) && type.is_base_action(action)) {
				base = action;
				break;
			}
		}

		// Cache for next time
		data->base = base;
		data->bases_checked = action_buffer.count();
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
	auto mask = 0u;

	if (type.input_predicate != nullptr)
		mask |= type.input_predicate(player, input);

	if (type.base_input_predicate != nullptr)
		mask |= type.base_input_predicate(player, input, base);

	// Don't detect the same inputs for the same action repeatedly in one frame
	mask &= ~data->detected_inputs;

	if (mask == 0)
		return;

	data->detected_inputs |= mask;
	last_action_poll[type_index] = poll_index;

	// Add the action multiple times if triggered multiple times in one poll
	while (mask != 0) {
		const auto input_type = std::countr_zero(mask);
		mask &= ~(1 << input_type);

		action_buffer.add({
			.type        = &type,
			.base_action = base,
			.poll_index  = poll_index,
			.input       = input,
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
	const auto queue_index = decrement_mod((int)HSD_PadLibData.qread, PAD_QNUM);

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

	if (end_index == invalid_index)
		return std::make_tuple(1, 0);

	return std::make_tuple(start_index, end_index);
}

EVENT_HANDLER(events::player::think::input::pre, [](Player *player)
{
	if (Player_IsCPU(player) || !player->update_inputs)
		return;

	// Store persistent data for detecting each action type
	action_detect_data detect_data[action_type_count] = { 0 };

	auto [start_index, end_index] = find_polls_for_frame(player->port);

	for (auto poll_index = start_index; poll_index <= end_index; poll_index++) {
		const auto *input = input_buffer[player->port].get(poll_index);

		if (input == nullptr)
			continue;

		const auto processed = processed_input(player, input->status);

		for (auto type_index = 0zu; type_index < action_type_count; type_index++) {
			detect_action_for_input(player, processed, poll_index, type_index,
			                        &detect_data[type_index]);
		}
	}
});

EVENT_HANDLER(events::player::think::input::post, [](Player *player, u32 old_state, u32 new_state)
{
	if (Player_IsCPU(player) || !player->update_inputs)
		return;

	const auto port = player->port;
	auto performed_action = false;

	for (size_t offset = 0; offset < action_buffer.stored(); offset++) {
		auto *action = action_buffer.tail(offset);
		if (action->port != port)
			continue;

		const auto *type = action->type;

		// Store player input if the action was performed this frame
		if (!action->final_input_set) {
			action->final_input = player->input;
			action->direction = player->direction;
			action->final_input_set = true;
		}

		if (!action->confirmed && type->success_predicate != nullptr) {
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
		}

		if (action->active && type->end_predicate != nullptr) {
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

EVENT_HANDLER(events::player::as_change, [](Player *player, u32 old_state, u32 new_state)
{
	for (const auto *type : action_types) {
		if (type->on_action_state != new_state)
			continue;

		const auto [start_index, end_index] = find_polls_for_frame(player->port);

		// Treat collision/damage state changes as being at the end of the frame
		const auto poll_index = curr_gobjproc == nullptr ||
		                        curr_gobjproc->s_link <= SLink_Input ? start_index
		                                                             : end_index + 1;

		action_buffer.add({
			.type        = type,
			.poll_index  = poll_index,
			.active      = true,
			.confirmed   = true,
			.success     = true,
			.port        = player->port
		});

		return;
	}
});

static void imgui_printer(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	ImGui::TextV(fmt, args);
	ImGui::SameLine(0, 0);
	va_end(args);
}

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

		if (action->type->hidden)
			continue;

		if (action->type->must_succeed && !action->success)
			continue;

		displayed++;

		// Check if base action got pushed out of the buffer
		if (base_action != nullptr && base_action->poll_index > action->poll_index)
			break;

                ImGui::TableNextRow();
		ImGui::TableNextColumn();

		if (action->success)
			ImGui::TextColored({0.2f, 1.f, 0.2f, 1.f}, "✔");
		else
			ImGui::TextColored({1.f, 0.2f, 0.2f, 1.f}, "❌");

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

		if (action->type->format_description != nullptr) {
			ImGui::SameLine();
			ImGui::Text("(");
			ImGui::SameLine(0, 0);
			action->type->format_description(action, imgui_printer);
			ImGui::Text(")");
		} else {
			const auto *input_name = action->type->input_names[action->input_type];
			if (input_name != nullptr) {
				ImGui::SameLine();
				ImGui::Text("(%s)", input_name);
			}
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
});

EVENT_HANDLER(events::match::exit, []()
{
	action_buffer.clear();
});