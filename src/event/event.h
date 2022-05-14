#pragma once

#include "util/hash.h"
#include "util/meta.h"
#include "util/preprocessor.h"

template<unsigned int id>
struct event_handler_type;

template<unsigned int id>
struct event_handler {
	const event_handler *next;
	const event_handler_type<id>::type *handle;

	event_handler(event_handler_type<id>::type *handle);
};

template<unsigned int id>
struct event_type {
	inline static const event_handler<id> *head;

	static bool fire(auto &&...args)
	{
		for (const auto *handler = head; handler != nullptr; handler = handler->next) {
			if (handler->handle(args...))
				return true;
		}

		return false;
	}
};

template<unsigned int id>
inline event_handler<id>::event_handler(event_handler_type<id>::type *handle)
	: handle(handle)
{
	next = event_type<id>::head;
	event_type<id>::head = this;
}

#define EVENT_ARGS(name, ...) \
	template<> \
	struct event_handler_type<hash<name>()> { using type = bool(__VA_ARGS__); }

#define FIRE_EVENT(name, ...) \
	event_type<hash<name>()>::fire(__VA_ARGS__)

#define EVENT_HANDLER(name, ...) \
	static event_handler<hash<name>()> CONCAT(_handler_, __COUNTER__)(__VA_ARGS__)
