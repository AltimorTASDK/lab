#pragma once

#include "console/console.h"
#include "util/hash.h"
#include "util/meta.h"
#include <limits>
#include <type_traits>

namespace console {

class cvar_base {
public:
	inline static cvar_base *head;
	cvar_base *next;

	const unsigned int name_hash;

protected:
	cvar_base(unsigned int name_hash) : name_hash(name_hash)
	{
		// Link into list
		next = head;
		head = this;
	}

public:
	virtual const char *get_name() = 0;
	virtual bool set_value(const char *str) = 0;
};

template<typename T, string_literal name>
class cvar : public cvar_base {
	struct cvar_callbacks {
		bool(*validate)(cvar *cv, const T &new_value);
		bool(*set)(cvar *cv, const T &new_value);
	};

	T value;
	const cvar_callbacks callbacks;
	const T min_value = std::numeric_limits<T>::lowest();
	const T max_value = std::numeric_limits<T>::max();

public:
	cvar(T value = {}, const cvar_callbacks &callbacks = {}) :
		cvar_base(hash<name>()),
		value(value),
		callbacks(callbacks)
	{
	}

	cvar(T value, T min_value, T max_value, const cvar_callbacks &callbacks = {}) :
		cvar(value, callbacks),
		min_value(min_value),
		max_value(max_value)
	{
	}

	const char *get_name() override
	{
		return name.value;
	}

	bool set_value(const char *str) override
	{
		const auto value = [&] {
			if constexpr (std::is_integral_v<T>)
				return strtol(str, &end, 0);
			else if constexpr (std::is_floating_point_v<T>)
				return strtod(str, &end);
			else
				static_assert(false, "Unsupported cvar type");
		}();

		if (*end != '\0') {
			if constexpr (std::is_integral_v<T>)
				console::print("Expected an integer value.");
			else if constexpr (std::is_floating_point_v<T>)
				console::print("Expected a floating point value.");

			return false;
		}

		if (value < min_value || value > max_value) {
			console::printf("Expected value in range [%d, %d].\n", min_value,
			                                                       max_value);
			return false;
		}

		return true;
	}
};

} // console