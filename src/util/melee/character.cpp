#include "melee/action_state.h"
#include "melee/player.h"
#include "melee/subaction.h"
#include "util/melee/character.h"
#include <cstring>

const ActionStateInfo *get_action_state_info(const Player *player, s32 state)
{
	if (state >= AS_CommonMax)
		return &player->character_as_table[state - AS_CommonMax];
	else
		return &player->common_as_table[state];
}

const char *get_state_subaction_name(const Player *player, s32 state)
{
	const auto subaction = get_action_state_info(player, state)->subaction;

	if (subaction == SA_None)
		return nullptr;

	return Player_GetSubactionInfo(player, subaction)->name;
}

special_type check_special_state(const Player *player, s32 state)
{
	if (state == AS_None)
		return special_type::none;

	const auto *name = get_state_subaction_name(player, state);

	if (name == nullptr)
		return special_type::none;

	name = strstr(name, "ACTION_Special");
	if (name == nullptr)
		return special_type::none;

	name += strlen("ACTION_Special");

	if (strncmp(name, "Air", 3) == 0)
		name += 3;

	switch (*name) {
	case 'S': return special_type::side;
	case 'H': return special_type::up;
	case 'L': return special_type::down;
	case 'N': return special_type::neutral;
	default:  return special_type::none;
	}
}