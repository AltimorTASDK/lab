#pragma once

// Convert a bitmask with bits corresponding to each input bool
constexpr auto bools_to_mask(auto ...values)
{
	auto bit = 0;
	return ((values ? (1 << bit++) : 0) | ...);
}