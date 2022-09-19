#pragma once

#include "labscript/result.h"
#include "util/hash.h"

namespace labscript {

enum class type {
	none,   // void
	s32,    // int
	f32,    // float
	player, // Player*
};

struct expression {
	virtual hash_t get_hash() = 0;

	virtual const char *get_name() = 0;
	virtual const char *get_description() = 0;

	virtual type get_type() = 0;

	virtual result execute(void *result) = 0;
};

} // namespace labscript