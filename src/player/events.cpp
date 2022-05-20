#include "player/events.h"
#include "util/hooks.h"

HOOK(Player_ASChange, [&](HSD_GObj *gobj, u32 new_state, u32 flags, HSD_GObj *parent,
                          f32 start_frame, f32 frame_rate, f32 lerp_override)
{
	auto *player = gobj->get<Player>();
	const auto old_state = player->action_state;

	original(gobj, new_state, flags, parent, start_frame, frame_rate, lerp_override);

	events::player::as_change.fire(player, old_state, new_state);

});