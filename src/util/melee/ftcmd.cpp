#include "melee/action_state.h"
#include "melee/ftcmd.h"
#include "melee/player.h"
#include "melee/subaction.h"
#include "util/melee/ftcmd.h"
#include <cfloat>
#include <gctypes.h>

static void parse_ftcmd(const Player *player, s32 subaction, auto &&callback)
{
	const auto *sa_info = Player_GetSubactionInfo(player, subaction);
	FtCmdState ftcmd = { .script = sa_info->script };

	while (true) {
		while (ftcmd.timer <= 0) {
			if (ftcmd.script == nullptr)
				return;

			const auto opcode = *ftcmd.script >> 2;

			if (callback(ftcmd, opcode))
				return;

			if (!FtCmd_ControlFlow(&ftcmd, opcode))
				ftcmd.script += FtCmdLength_Player[opcode - 10] * 4;
		}

		if (ftcmd.timer == FLT_MAX) {
			if (ftcmd.frame >= 1)
				return;

			ftcmd.timer = -ftcmd.frame;
		}

		if (ftcmd.timer > 0) {
			const auto delay = std::ceil(ftcmd.timer);
			ftcmd.frame += delay;
			ftcmd.timer -= delay;
		}
	}
}

static int find_flag_set(const Player *player, s32 subaction, u32 flag, bool value)
{
	int frame = -1;

	parse_ftcmd(player, subaction, [&](const FtCmdState &ftcmd, u32 opcode) {
		const auto *arg = (FtCmdArg_SetFlag*)ftcmd.script;

		if (opcode == FtCmd_SetFlag && arg->flag == flag && (bool)arg->value == value) {
			frame = (u32)ftcmd.frame - 1;
			return true;
		}

		return false;
	});

	return frame;
}

static ActionStateInfo *get_action_state_info(const Player *player, s32 state)
{
	if (state >= AS_CommonMax)
		return player->character_as_table[state - AS_CommonMax];
	else
		return player->common_as_table[state];
}

static int initial_dash_cache[CID_Max] = { -1 };

int get_initial_dash(const Player *player)
{
	auto *cache = initial_dash_cache[player->character_id];

	if (*cache == -1)
		*cache = find_flag_set(player, SA_Dash, 0, true);

	return *cache;
}

static int multijump_cooldown_cache[CID_Max] = { -1 };

int get_multijump_cooldown(const Player *player)
{
	if (!player->multijump)
		return -1;

	auto *cache = multijump_cooldown_cache[player->character_id];

	if (*cache == -1) {
		const auto state = player->extra_stats.multijump_stats->start_state;
		const auto subaction = get_action_state_info(player, state)->subaction;
		*cache = find_flag_set(player, subaction, 0, true);
	}

	return *cache;
}