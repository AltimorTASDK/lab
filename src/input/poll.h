#pragma once

#include "os/serial.h"
#include "event/event.h"

namespace events::input {
inline event<void(s32 chan, const SIPadStatus &pad)> poll;
} // events::input