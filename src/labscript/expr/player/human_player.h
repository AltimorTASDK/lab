#pragma once

#include "melee/player.h"
#include "labscript/expression.h"
#include "labscript/internal.h"

namespace labscript::expr {

struct human_player : expression {
	hash_t get_hash() const override
	{
		return hash<"human_player">();
	}

	type get_type() const override
	{
		return type::player;
	}

	result execute(void *result) const override
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

LABSCRIPT_EXPR_TYPE(human_player, "Human Player", "Get the human player with the lowest port.");

} // namespace labscript::expr