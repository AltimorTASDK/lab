#pragma once

#include "util/preprocessor.h"
#include <type_traits>

template<typename T>
struct hook_entry {
	T orig;
	T hook;
};

#define HOOK(_function, ...) \
	namespace CONCAT(_hook_, __COUNTER__) { \
	static constexpr auto function = (_function); \
	[[gnu::section(".hooks")]] [[gnu::used]] \
	static hook_entry<decltype(function)> entry = { \
		function, \
		[]<typename ret, typename ...args>(ret(*)(args...)) { \
			return [](args ...va) -> ret { \
				const auto original = (ret(*)(args...))&entry; \
				const auto lambda = (__VA_ARGS__); \
				static_assert(requires { lambda(va...); }, \
					"Wrong hook parameter types"); \
				static_assert(std::is_same_v<decltype(lambda(va...)), ret>, \
					"Wrong hook return type"); \
				return lambda(va...); \
			}; \
		}(function) \
	}; \
	} \
	static_assert(true) // Force semicolon
