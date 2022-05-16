#include "os/serial.h"
#include "os/vi.h"
#include "console/console.h"
#include "console/cvar.h"
#include "input/poll.h"
#include "util/hash.h"
#include "util/hooks.h"
#include <cstdio>
#include <cstring>

static s32 poll_index[4];
static u32 last_retrace_count[4];
static SIPadStatus status[4];

static console::cvar<int> polling_mult("polling_mult", {
	.value = 1, .min = 1, .max = 8,
	.set = [](int) {
		// Force polling rate update
		const auto sipoll = Si.poll.raw;
		SI_SetSamplingRate(16);
		SI_DisablePolling(0b11110000 << 24);
		SI_EnablePolling(sipoll << 24);
	}});

HOOK(SI_SetXY, [&](u16 line, u8 cnt)
{
	// Change poll interval so the middle poll happens when it would on vanilla
	original((u16)(line / polling_mult.get()), (u8)(cnt * polling_mult.get()));
});

HOOK(SI_GetResponseRaw, [&](s32 chan)
{
	const auto result = original(chan);
	const auto copy = (SIPadStatus&)SICHANNEL[chan].in.status;

	const auto retrace_count = VIGetRetraceCount();
	if (retrace_count > last_retrace_count[chan])
		poll_index[chan] = 0;

	if (result)
		events::input::poll.fire(chan, copy);

	// Store the polls that would happen at 120Hz
	if (!result)
		status[chan] = { 0 };
	else if (poll_index[chan] == 0 || poll_index[chan] == Si.poll.y / 2)
		status[chan] = copy;

	poll_index[chan]++;
	last_retrace_count[chan] = retrace_count;

	return result;
});

HOOK(SI_GetResponse, [&](s32 chan, void *buf)
{
	// Use 120Hz polls
	const auto result = original(chan, buf);
	if (result && polling_mult.get() != 1)
		*(SIPadStatus*)buf = status[chan];

	return result;
});