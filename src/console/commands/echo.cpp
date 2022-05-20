#include "console/console.h"
#include "util/hash.h"
#include <cstring>

EVENT_HANDLER(events::console::cmd, [](unsigned int cmd_hash, int argc, const char *argv[])
{
	if (cmd_hash != hash<"echo">())
		return false;

	if (argc >= 2)
		console::print(argv[1]);

	return true;
});