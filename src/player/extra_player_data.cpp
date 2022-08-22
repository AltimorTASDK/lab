#include "match/events.h"
#include "player/extra_player_data.h"

EVENT_HANDLER(events::match::exit, []()
{
	extra_player_data_detail::match_number++;
});