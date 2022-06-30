#pragma once

#include "melee/player.h"

template<typename T>
class extra_player_data {
	T data[12];

public:
	T *get(const Player *player)
	{
		return &data[player->slot * 2 + player->is_backup_climber];
	}

	const T *get(const Player *player) const
	{
		return &data[player->slot * 2 + player->is_backup_climber];
	}
};