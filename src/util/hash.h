#pragma once

#include "util/meta.h"

namespace fnv1a {
constexpr auto offset_basis = 0x811C9DC5u;
constexpr auto prime        = 0x01000193u;
} // fnv1a

// Consteval FNV1a hash
template<string_literal str>
consteval unsigned int hash()
{
	auto hash = fnv1a::offset_basis;

	for_range<str.size>([&]<size_t ...N> {
		((hash = (hash ^ str.value[N]) * fnv1a::prime), ...);
	});

	return hash;
}

// Consteval FNV1a hash
template<size_t size>
consteval unsigned int hash(const char (&str)[size])
{
	auto hash = fnv1a::offset_basis;

	for_range<size>([&]<size_t ...N> {
		((hash = (hash ^ str[N]) * fnv1a::prime), ...);
	});

	return hash;
}

// Runtime FNV1a hash
inline unsigned int hash(const char *str)
{
	auto hash = fnv1a::offset_basis;

	for (auto c = *str; c != '\0'; str++, c = *str)
		hash = (hash ^ c) * fnv1a::prime;

	return hash;
}