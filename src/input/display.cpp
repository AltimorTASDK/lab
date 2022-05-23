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
#include "util/machine.h"
#include "util/math.h"
#include "util/ring_buffer.h"
#include "util/vector.h"
#include "util/melee/pad.h"
#include <imgui.h>
#include <ogc/machine/asm.h>

constexpr auto INPUT_BUFFER_SIZE = MAX_POLLS_PER_FRAME * PAD_QNUM;
constexpr auto INPUT_SEQUENCE_SIZE = 20;
constexpr auto INPUT_SEQUENCE_HISTORY = 10;

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

struct input_sequence {
	const input_sequence_type *type;
	u8 port;
	u32 start_poll_num;
	u32 start_retrace_count;
	ring_buffer<const saved_input*, INPUT_SEQUENCE_SIZE> inputs;
};

static const input_sequence_type input_sequence_types[] = {
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

static ring_buffer<saved_input, INPUT_BUFFER_SIZE> input_buffer[4];
static ring_buffer<input_sequence, INPUT_SEQUENCE_HISTORY> input_sequences;

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat != 0)
		return;

	const auto *last_input = input_buffer[chan].head();
	const auto last_buttons = last_input != nullptr ? last_input->buttons : 0;

	INTERRUPT_FPU_ENABLE();

	input_buffer[chan].add({
		.qwrite   = HSD_PadLibData.qwrite,
		.buttons  = status.buttons,
		.pressed  = (status.buttons ^ last_buttons) &  status.buttons,
		.released = (status.buttons ^ last_buttons) & ~status.buttons,
		.stick    = convert_hw_coords(status.stick),
		.cstick   = convert_hw_coords(status.cstick)
	});
});

static u32 find_sequence_start_poll(const Player *player, const input_sequence_type &type)
{
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
		return head_index;
	}

	for (auto index = start_index; index <= end_index; index++) {
		const auto *input = buffer.get(index);

		if (input != nullptr && type.start_predicate(player, *input))
			return index;
	}

	OSReport("Failed to find start of input sequence\n");
	return head_index;
}

EVENT_HANDLER(events::player::as_change, [](Player *player, u32 old_state, u32 new_state)
{
	if (Player_IsCPU(player))
		return;

	for (const auto &type : input_sequence_types) {
		if (type.start_state != new_state)
			continue;

		const auto start_poll = find_sequence_start_poll(player, type);
		OSReport("input sequence started on poll %u\n", start_poll);

		input_sequences.add({
			.type                = &type,
			.port                = player->port,
			.start_poll_num      = start_poll,
			.start_retrace_count = VIGetRetraceCount(),
			.inputs              = { { input_buffer[player->port].get(start_poll) } }
		});

		return;
	}
});