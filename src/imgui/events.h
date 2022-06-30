#pragma once

#include "event/event.h"

namespace events::imgui {
inline event<void()> draw;
inline event<void()> init;
inline event<bool()> capture_input;
} // events::imgui