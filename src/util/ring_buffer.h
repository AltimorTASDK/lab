#pragma once

#include "util/math.h"
#include <optional>

template<typename T, ssize_t N>
class ring_buffer {
	T data[N];
	ssize_t next_index = 0;

public:
	bool is_valid_index(ssize_t index)
	{
		return index >= std::max(0, next_index - N) && index < next_index;
	}

	const T *get(ssize_t index)
	{
		return is_valid_index(index) ? &data[mod(index, N)] : nullptr;
	}

	bool set(ssize_t index, const T &value)
	{
		if (!is_valid_index(index))
			return false;

		data[mod(index, N)] = value;
		return true;
	}

	void add(const T &value)
	{
		data[mod(next_index++, N)] = value;
	}

	const ssize_t head_index()
	{
		return next_index - 1;
	}

	const T *head(ssize_t offset = 0)
	{
		return get(head_index() - offset);
	}
};