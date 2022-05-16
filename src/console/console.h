#include "event/event.h"

namespace events::console {
inline event<bool(unsigned int cmd_hash, const char *line)> cmd;
} // events::console

namespace console {

void print(const char *line);
void printf(const char *fmt, ...);

} // console