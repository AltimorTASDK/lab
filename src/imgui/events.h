#pragma once

#include "event/event.h"

namespace events::imgui {
inline event<void()> draw;
inline event<void()> init;
} // events::imgui