#pragma once

#include "labscript/result.h"
#include "util/hash.h"
#include "util/preprocessor.h"

#define LABSCRIPT_EXPR_TYPE(type, name, description)                                               \
	namespace CONCAT(_expr_type_, __COUNTER__) {                                               \
	static auto new_type = expr_type(name, description,                                        \
	                                 [] { return static_cast<expression*>(new type); });       \
	}                                                                                          \
	static_assert(true) // Force semicolon

namespace labscript {

enum class type {
	none,   // void
	s32,    // int
	f32,    // float
	player, // Player*
};

struct expression {
	// Next expression within this scope.
	expression *next;
	// The first expression of the scope enclosed by this expression.
	expression *child;
	// The first expression passed as an argument to this expression.
	expression *input;

	virtual ~expression() = default;

	// Get a unique hash to be used for encoding.
	virtual hash_t get_hash() const = 0;

	// Get the type of the result.
	virtual type get_type() const = 0;

	// Execute/evaluate the expression and set the result passed if not null.
	// result must point to an object corresponding to the type returned by get_type.
	virtual result execute(void *result) const = 0;

	// Whether this expression encloses a child scope. Used for flow control.
	virtual bool has_child() const
	{
		return false;
	}
};

struct expr_type {
	using constructor_t = expression*();

	inline static expr_type *head;

	// Next expression type in list
	expr_type *next;
	// Pretty name for UI display
	const char *const name;
	// Pretty description for UI display
	const char *const description;
	// Function to create an instance of this expression
	constructor_t *const constructor;

	expr_type(const char *name, const char *description, constructor_t *constructor)
		: name(name), description(description), constructor(constructor)
	{
		next = head;
		head = this;
	}
};

} // namespace labscript