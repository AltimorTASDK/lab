#pragma once

#include "os/serial.h"
#include "event/event.h"

constexpr auto MAX_POLLS_PER_FRAME = 32;

namespace events::input {
inline event<void(s32 chan, const SIPadStatus &pad)> poll;
} // events::input