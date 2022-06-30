#pragma once

// Convert a bitmask with bits corresponding to each input bool
constexpr auto bools_to_mask(auto ...values)
{
	auto bit = 0;
	auto result = 0;
	((result |= values ? (1 << bit) : 0, bit++), ...);
	return result;
}

// Check if every bit in mask is set in value
constexpr bool all_set(auto value, auto mask)
{
	return (value & mask) == mask;
}