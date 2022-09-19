#pragma once

#include "melee/player.h"
#include "labscript/expression.h"
#include "labscript/internal.h"

namespace labscript::expr {

struct human_player : expression {
	hash_t get_hash() override
	{
		return hash<"human_player">();
	}

	const char *get_name() override
	{
		return "Human Player";
	}

	const char *get_description() override
	{
		return "Get the human player with the lowest port.";
	}

	type get_type() override
	{
		return type::player;
	}

	result execute(void *result) override
	{
		Player *player = nullptr;

		for (auto i = 0; i < 6; i++) {
			if (PlayerBlock_GetSlotType(i) == SlotType_Human) {
				player = PlayerBlock_GetGObj(i)->get<Player>();
				break;
			}
		}

		set_result(result, player);
		return result::ok;
	}
};

} // namespace labscript::expr