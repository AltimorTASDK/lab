#include "melee/match.h"
#include "match/events.h"
#include "util/hooks.h"

HOOK(Match_Exit, [&](MatchExitData *data)
{
	events::match::exit.fire();
	original(data);
});