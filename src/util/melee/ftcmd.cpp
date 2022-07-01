#include "melee/ftcmd.h"
#include "melee/player.h"
#include "melee/subaction.h"
#include "util/melee/ftcmd.h"
#include <cfloat>
#include <gctypes.h>

#warning temp
#include "os/os.h"

void parse_ftcmd(const Player *player, u32 subaction, auto &&callback)
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

		while (ftcmd.timer > 0) {
			ftcmd.frame++;
			ftcmd.timer--;
		}
	}
}

static u32 initial_dash_cache[CID_Max] = { 0 };

u32 get_initial_dash(const Player *player)
{
	const auto char_id = player->character_id;

	if (initial_dash_cache[char_id] != 0)
		return initial_dash_cache[char_id];

	parse_ftcmd(player, SA_Dash, [&](const FtCmdState &ftcmd, u32 opcode) {
		const auto *arg = (FtCmdArg_SetFlag*)ftcmd.script;

		if (opcode == FtCmd_SetFlag && arg->flag == 0 && arg->value != 0) {
			initial_dash_cache[char_id] = (u32)ftcmd.frame - 1;
			return true;
		}

		return false;
	});

	return initial_dash_cache[char_id];
}