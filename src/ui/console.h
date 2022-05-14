#include "event/event.h"

EVENT_ARGS("console.cmd", unsigned int cmd_hash, const char *line);

namespace console {

void print(const char *line);
void printf(const char *fmt, ...);

} // console