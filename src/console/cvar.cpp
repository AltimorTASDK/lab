#include "console/cvar.h"
#include "console/console.h"

static event_handler cmd_handler(&events::console::cmd,
	[](unsigned int cmd_hash, int argc, const char *argv[])
{
	for (auto *cvar : console::cvar_base::iterate()) {
		if (cmd_hash != cvar->name_hash)
			continue;

		if (argc >= 2)
			cvar->set_value(argv[1]);
		else
			console::print("Expected a value.");

		return true;
	}

	return false;
});