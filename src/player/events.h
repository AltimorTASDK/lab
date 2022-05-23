#pragma once

#include "melee/player.h"
#include "event/event.h"
#include <gctypes.h>

namespace events::player {
inline event<void(Player *player, u32 old_state, u32 new_state)> as_change;
} // events::player

namespace events::player::think::input {
inline event<void(Player *player)> pre;
inline event<void(Player *player)> post;
} // events::player::think::input