#pragma once

#include "melee/player.h"

namespace extra_player_data_detail {
inline size_t match_number;
} // extra_player_data_detail

template<typename T>
class extra_player_data {
	mutable T data[12];
	mutable size_t match_number;

public:
	const T *get(const Player *player) const
	{
		if (match_number != extra_player_data_detail::match_number) {
			// New match started, reset data
			for (auto &elem : data)
				new (&elem) T;

			match_number = extra_player_data_detail::match_number;
		}

		return &data[player->slot * 2 + player->is_secondary_char];
	}

	T *get(const Player *player)
	{
		return const_cast<T*>(const_cast<const extra_player_data&>(*this).get(player));
	}

};