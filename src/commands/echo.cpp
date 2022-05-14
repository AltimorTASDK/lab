#include "ui/console.h"
#include <cstring>

EVENT_HANDLER("console.cmd", [](unsigned int cmd_hash, const char *line)
{
	if (cmd_hash != hash<"echo">())
		return false;

	const char *msg = strchr(line, ' ');
	if (*msg != '\0')
		msg++;

	console::print(msg);
	return true;
});