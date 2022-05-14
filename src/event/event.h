#pragma once

#include <type_traits>
#include <utility>

template<typename callback_type>
struct event;

template<typename callback_type>
struct event_handler;

template<typename ret, typename ...args>
struct event<ret(args...)> {
	using callback_type = ret(args...);

	const event_handler<callback_type> *head;

	bool fire(args ...va)
	{
		for (const auto *handler = head; handler != nullptr; handler = handler->next) {
			if constexpr (std::is_same_v<ret, void>) {
				handler->handle(std::forward<args>(va)...);
			} else {
				if (handler->handle(std::forward<args>(va)...))
					return true;
			}
		}

		return false;
	}
};

template<typename ret, typename ...args>
struct event_handler<ret(args...)> {
	using callback_type = ret(args...);

	const event_handler *next;
	callback_type *handle;

	event_handler(event<callback_type> *ev, callback_type *handle) : handle(handle)
	{
		next = ev->head;
		ev->head = this;
	}
};

template<typename callback_type>
event_handler(event<callback_type>*, auto) -> event_handler<callback_type>;