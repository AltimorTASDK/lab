#pragma once

#include "util/hash.h"
#include "util/meta.h"
#include "util/preprocessor.h"
#include <type_traits>

#define EVENT_ARGS(name, ...) \
	template<> struct event::handler_type<hash<name>()> { using type = bool(__VA_ARGS__); }

#define EVENT_ARGS_VOID(name, ...) \
	template<> struct event::handler_type<hash<name>()> { using type = void(__VA_ARGS__); }

#define EVENT_HANDLER(name, ...) \
	[[gnu::used]] \
	static const event::handler<hash<name>()> CONCAT(_handler_, __COUNTER__)(__VA_ARGS__)

namespace event {

template<unsigned int id>
struct handler_type;

template<unsigned int id>
struct handler {
	const handler *next;
	const handler_type<id>::type *handle;

	handler(handler_type<id>::type *handle);
};

template<unsigned int id>
struct type {
	inline static const handler<id> *head;

	static bool fire(auto &&...args)
	{
		using ret_type = decltype(head->handle(std::forward<decltype(args)>(args)...));

		for (const auto *handler = head; handler != nullptr; handler = handler->next) {
			if constexpr (std::is_same_v<ret_type, void>) {
				handler->handle(std::forward<decltype(args)>(args)...);
			} else {
				if (handler->handle(std::forward<decltype(args)>(args)...))
					return true;
			}
		}

		return false;
	}
};

template<unsigned int id>
inline handler<id>::handler(handler_type<id>::type *handle)
	: handle(handle)
{
	next = type<id>::head;
	type<id>::head = this;
}

template<string_literal name>
inline auto fire(auto &&...args)
{
	return type<hash<name>()>::fire(std::forward<decltype(args)>(args)...);
}

} // namespace event