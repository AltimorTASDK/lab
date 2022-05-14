#include "os/serial.h"
#include "os/vi.h"
#include "input/subframe.h"
#include "ui/console.h"
#include "util/hash.h"
#include "util/hooks.h"
#include <cstdio>
#include <cstring>

static u32 polling_mult = 1;
static s32 poll_index[4] = { 0 };
static u32 last_retrace_count[4] = { 0 };
static SIPadStatus status[4];

static event_handler cmd_handler(&events::console::cmd, [](unsigned int cmd_hash, const char *line)
{
	if (cmd_hash != hash<"polling_mult">())
		return false;

	int new_mult;
	if (sscanf(line, "%*32s %d", &new_mult) != 1 || new_mult < 1 || new_mult > 8) {
		console::print("Usage: polling_mult <1-8>");
		return true;
	}

	polling_mult = new_mult;

	// Force polling rate update
	const auto sipoll = Si.poll.raw;
	SI_SetSamplingRate(16);
	SI_DisablePolling(0b11110000 << 24);
	SI_EnablePolling(sipoll << 24);

	return true;
});

HOOK(SI_SetXY, [&](u16 line, u8 cnt)
{
	// Change poll interval so the last poll happens when it would on vanilla
	const auto new_cnt = (u8)(cnt * polling_mult);
	const auto new_line = (u16)(line * (cnt - 1) / (new_cnt - 1));
	original(new_line, new_cnt);
});

HOOK(SI_GetResponseRaw, [&](s32 chan)
{
	const auto result = original(chan);

	if (result)
		events::input::poll.fire((SIPadStatus&)SICHANNEL[chan].in.status);

	if (polling_mult == 1)
		return result;

	const auto retrace_count = VIGetRetraceCount();
	if (VIGetRetraceCount() > last_retrace_count[chan])
		poll_index[chan] = 0;

	// Store the polls that would happen at 120Hz
	if (poll_index[chan] == 0 || poll_index[chan] == Si.poll.y - 1)
		status[chan] = (SIPadStatus&)SICHANNEL[chan].in.status;

	poll_index[chan]++;
	last_retrace_count[chan] = retrace_count;

	return result;
});

HOOK(SI_GetResponse, [&](s32 chan, void *buf)
{
	// Use 120Hz polls
	const auto result = original(chan, buf);
	if (result && polling_mult != 1)
		*(SIPadStatus*)buf = status[chan];

	return result;
});