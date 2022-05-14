#pragma once

#include "os/serial.h"
#include "event/event.h"

namespace events::input {
inline event<void(const SIPadStatus &pad)> poll;
} // events::input