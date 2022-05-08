#pragma once

#include "util/preprocessor.h"

template<typename T>
struct hook_entry {
	T *orig;
	T *hook;
};

#define HOOK(function, ...) \
	namespace CONCAT(_hook_, __COUNTER__) { \
	[[gnu::section(".hooks")]] [[gnu::used]] \
	static hook_entry<decltype(function)> entry = { \
		function, \
		[]<typename ret, typename ...args>(ret(*)(args...)) { \
			return [](args ...va) -> ret { \
				const auto original = (ret(*)(args...))(&entry); \
				return __VA_ARGS__(va...); \
			}; \
		}(function) \
	}; \
	}