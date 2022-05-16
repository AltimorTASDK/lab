#pragma once

#include "console/console.h"
#include "util/hash.h"
#include "util/meta.h"
#include <limits>
#include <type_traits>

namespace console {

class cvar_base {
public:
	struct iterator {
		cvar_base *cv;

		iterator(cvar_base *cv) : cv(cv)
		{
		}

		iterator &begin()
		{
			return *this;
		}

		iterator end()
		{
			return iterator(nullptr);
		}

		iterator &operator++()
		{
			cv = cv->next;
			return *this;
		}

		cvar_base *operator*()
		{
			return cv;
		}

		bool operator<=>(const iterator &other) const = default;
	};

private:
	inline static cvar_base *head;
	cvar_base *next;

public:
	const unsigned int name_hash;
	const char *const name;

protected:
	template<size_t N>
	cvar_base(const char (&name)[N]) :
		name_hash(hash(name)),
		name(name)
	{
		// Link into list
		next = head;
		head = this;
	}

public:
	static iterator iterate()
	{
		return iterator(head);
	}

	virtual bool set_value(const char *str) = 0;
};

template<typename T>
class cvar : public cvar_base {
	struct cvar_params {
		T value = {};
		T min = std::numeric_limits<T>::lowest();
		T max = std::numeric_limits<T>::max();
		bool(*validate)(T value);
		void(*set)(T value);
	};

	const cvar_params params;
	T value;

public:
	template<size_t N>
	cvar(const char (&name)[N], const cvar_params &params = {}) :
		cvar_base(name),
		params(params),
		value(params.value)
	{
	}

	T get()
	{
		return value;
	}

	bool set_value(const char *str) override
	{
		char *end;

		const auto new_value = [&] {
			if constexpr (std::is_integral_v<T>)
				return strtol(str, &end, 0);
			else if constexpr (std::is_floating_point_v<T>)
				return strtod(str, &end);
		}();

		if (*end != '\0') {
			if constexpr (std::is_integral_v<T>)
				console::print("Expected an integer value.");
			else if constexpr (std::is_floating_point_v<T>)
				console::print("Expected a floating point value.");

			return false;
		}

		if (new_value < params.min || new_value > params.max) {
			if constexpr (std::is_integral_v<T>) {
				console::printf("Expected value in range [%ld, %ld].\n",
				                params.min, params.max);
			} else if constexpr (std::is_floating_point_v<T>) {
				console::printf("Expected value in range [%lf, %lf].\n",
				                params.min, params.max);
			}
			return false;
		}

		if (params.validate != nullptr && !params.validate((T)new_value))
			return false;

		value = (T)new_value;

		if (params.set != nullptr)
			params.set(value);

		return true;
	}
};

} // console