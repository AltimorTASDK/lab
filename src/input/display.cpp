#include "os/context.h"
#include "os/os.h"
#include "os/serial.h"
#include "hsd/pad.h"
#include "console/console.h"
#include "imgui/draw.h"
#include "input/poll.h"
#include <cstring>
#include <imgui.h>
#include <ogc/machine/asm.h>

static u32 poll_count[4];
static u32 pressed_buttons[4];
static u32 press_interval[4];
static u32 last_press_time[4];
static SIPadStatus last_status[4];

static event_handler poll_handler(&events::input::poll, [](s32 chan, const SIPadStatus &status)
{
	if (status.errstat != 0)
		return;

	const auto pressed = (status.buttons ^ last_status[chan].buttons) & status.buttons;

	if (pressed != 0) {
		pressed_buttons[chan] = pressed;
		press_interval[chan] = poll_count[chan] - last_press_time[chan];
		last_press_time[chan] = poll_count[chan];

		char buttons[8] = { '\0' };
		if (pressed & Button_A)
			strlcat(buttons, "A", sizeof(buttons));
		if (pressed & Button_B)
			strlcat(buttons, "B", sizeof(buttons));
		if (pressed & Button_X)
			strlcat(buttons, "X", sizeof(buttons));
		if (pressed & Button_Y)
			strlcat(buttons, "Y", sizeof(buttons));
		if (pressed & Button_L)
			strlcat(buttons, "L", sizeof(buttons));
		if (pressed & Button_R)
			strlcat(buttons, "R", sizeof(buttons));
		if (pressed & Button_Z)
			strlcat(buttons, "Z", sizeof(buttons));

		OSContext context;
		PPCMtmsr(PPCMfmsr() | MSR_FP);
		OSSaveFPUContext(&context);

		console::printf("% 7s %.01ff", buttons, (float)press_interval[chan] / Si.poll.y);

		OSLoadFPUContext(&context);
		PPCMtmsr(PPCMfmsr() & ~MSR_FP);
	}

	poll_count[chan]++;
	last_status[chan] = status;
});