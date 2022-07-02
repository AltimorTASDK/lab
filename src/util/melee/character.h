#include "melee/player.h"
#include <gctypes.h>

enum class special_type {
	none,
	neutral,
	side,
	up,
	down
};

const ActionStateInfo *get_action_state_info(const Player *player, s32 state);
const char *get_state_subaction_name(const Player *player);
special_type check_special_state(const Player *player, s32 state);

inline float get_subaction_length(const Player *player, s32 subaction)
{
	return Player_GetFigaTree(player, subaction)->frames;
}