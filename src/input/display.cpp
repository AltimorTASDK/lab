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
constexpr auto ACTION_HISTORY = 10;

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
		pressed((status.buttons ^ player->input.last_held_buttons) &  status.buttons),
		released((status.buttons ^ player->input.last_held_buttons) & ~status.buttons),
		stick(convert_hw_coords(status.stick)),
		cstick(convert_hw_coords(status.cstick))
	{
	}
};

struct action_type {
	const char *name;
	// Predicate to detect state/inputs that trigger this action (before PlayerThink_Input)
	bool(*input_predicate)(const Player *player, const processed_input &input);
	// Predicate to detect whether the action succeeded (after PlayerThink_Input)
	bool(*success_predicate)(const Player *player);
};

struct action {
	const action_type *type;
	u32 poll_num;
	bool success = false;
	u8 port;
};

static const action_type action_types[] = {
	{
		.name = "Jump",
		.input_predicate = [](const Player *player, const processed_input &input) {
			if (player->airborne)
				return false;

			return (input.pressed & (Button_X | Button_Y)) ||
			       (input.stick.y >= plco->y_smash_threshold &&
			        player->input.stick_y_hold_time < plco->y_smash_frames);
		},
		.success_predicate = [](const Player *player) {
			return player->action_state == AS_KneeBend;
		}
	}
};

static ring_buffer<saved_input, INPUT_BUFFER_SIZE> input_buffer[4];
static ring_buffer<action, ACTION_HISTORY> action_buffer;
static u32 last_confirmed_action[4];

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat == 0)
		input_buffer[chan].add({ .qwrite = HSD_PadLibData.qwrite, .status = status });
});

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

	for (auto index = start_index; index <= end_index; index++) {
		const auto *input = buffer.get(index);

		if (input == nullptr)
			continue;

		const auto processed = processed_input(player, input->status);

		for (const auto &type : action_types) {
			if (!type.input_predicate(player, processed))
				continue;

			action_buffer.add({
				.type     = &type,
				.poll_num = index,
				.port     = player->port
			});
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

		if (action != nullptr && action->type->success_predicate(player))
			action->success = true;
	}
});