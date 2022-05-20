#include "os/context.h"
#include "os/os.h"
#include "os/serial.h"
#include "hsd/pad.h"
#include "melee/action_state.h"
#include "melee/player.h"
#include "console/console.h"
#include "imgui/draw.h"
#include "input/poll.h"
#include "player/events.h"
#include "util/hooks.h"
#include <cstring>
#include <imgui.h>
#include <ogc/machine/asm.h>

static struct {
	u32 poll_count;
	u32 last_press_time[16];
	SIPadStatus status;
} port_state[4];

EVENT_HANDLER(events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat != 0)
		return;

	auto *state = &port_state[chan];
	auto pressed = (status.buttons ^ state->status.buttons) & status.buttons;

	while (pressed != 0) {
		const auto bit = __builtin_ctz(pressed);
		state->last_press_time[bit] = state->poll_count;
		pressed &= ~(1 << bit);
	}

	state->poll_count++;
	state->status = status;
});

EVENT_HANDLER(events::player::as_change, [](Player *player, u32 old_state, u32 new_state)
{
});