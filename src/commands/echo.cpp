#include "ui/console.h"
#include "util/hash.h"
#include <cstring>

static event_handler cmd_handler(&events::console::cmd, [](unsigned int cmd_hash, const char *line)
{
	if (cmd_hash != hash<"echo">())
		return false;

	const char *msg = strchr(line, ' ');
	while (*msg == ' ')
		msg++;

	console::print(msg);
	return true;
});