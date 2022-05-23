#include "os/serial.h"
#include "hsd/pad.h"
#include "melee/action_state.h"
#include "melee/constants.h"
#include "melee/player.h"
#include "console/console.h"
#include "imgui/draw.h"
#include "input/poll.h"
#include "player/events.h"
#include "util/hooks.h"
#include "util/machine.h"
#include "util/math.h"
#include "util/ring_buffer.h"
#include "util/vector.h"
#include "util/melee/pad.h"
#include <imgui.h>
#include <ogc/machine/asm.h>

constexpr auto INPUT_BUFFER_SIZE = MAX_POLLS_PER_FRAME * 10;

struct saved_input {
	u8 qwrite;
	u32 buttons;
	u32 pressed;
	u32 released;
	vec2 stick;
	vec2 cstick;
};

using input_predicate = bool(const Player *player, const saved_input &input);

struct input_sequence_type {
	// Action state to start sequence upon entering
	u32 start_state;
	// Predicate to detect input that started the sequence
	input_predicate *start_predicate;
	// Predicate to detect inputs that add to the sequence
	input_predicate *followup_predicate;
};

static input_sequence_type input_sequence_types[] = {
	{
		.start_state = AS_KneeBend,
		.start_predicate = [](const Player *player, const saved_input &input) {
			return (input.pressed & (Button_X | Button_Y)) ||
			       (input.stick.y >= plco->y_smash_threshold &&
			        player->input.stick_y_hold_time < plco->y_smash_frames);
		},
		.followup_predicate = [](const Player *player, const saved_input &input) {
			return input.pressed != 0;
		}
	}
};

static struct {
	u32 poll_count;
	ring_buffer<saved_input, INPUT_BUFFER_SIZE> input_buffer;
} port_state[4];

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat != 0)
		return;

	const auto *last_input = port_state[chan].input_buffer.head();

	INTERRUPT_FPU_ENABLE();

	const auto input = saved_input {
		.qwrite   = HSD_PadLibData.qwrite,
		.buttons  = status.buttons,
		.pressed  = (status.buttons ^ last_input->buttons) &  status.buttons,
		.released = (status.buttons ^ last_input->buttons) & ~status.buttons,
		.stick    = convert_hw_coords(status.stick),
		.cstick   = convert_hw_coords(status.cstick)
	};

	port_state[chan].input_buffer.add(input);
});

EVENT_HANDLER(events::player::as_change, [](Player *player, u32 old_state, u32 new_state)
{
});