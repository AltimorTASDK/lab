#include "dolphin/serial.h"
#include "dolphin/vi.h"
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
	.value = 10, .min = 1, .max = MAX_POLLS_PER_FRAME / 2,
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
	const auto new_cnt = (u8)std::min(cnt * polling_mult.get(), MAX_POLLS_PER_FRAME);
	const auto progressive = vi_regs->viclk.s != 0;

	if (progressive)
		original((u16)(525 / new_cnt), new_cnt);
	else
		original((u16)(525 / new_cnt / 2), new_cnt);
});

HOOK(SI_GetResponseRaw, [&](s32 chan)
{
	const auto result = original(chan);
	const auto &new_status = SILastPadStatus[chan];

	const auto retrace_count = VIGetRetraceCount();
	if (retrace_count > last_retrace_count[chan])
		poll_index[chan] = 0;

	events::input::poll.fire(chan, new_status);

	// Store the polls that would happen at 120Hz
	if (poll_index[chan] == 0 || poll_index[chan] == Si.poll.y / 2)
		status[chan] = new_status;

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