#pragma once

#include <algorithm>
#include <initializer_list>

template<typename T, size_t N>
class ring_buffer {
	T data[N];
	size_t next_index = 0;

public:
	ring_buffer() = default;

	ring_buffer(std::initializer_list<T> list)
	{
		for (const auto &value : list)
			add(value);
	}

	size_t capacity() const
	{
		return N;
	}

	size_t count() const
	{
		return next_index;
	}

	size_t stored() const
	{
		return std::min(count(), N);
	}

	void clear()
	{
		next_index = 0;
	}

	bool is_valid_index(size_t index) const
	{
		// Relies on unsigned overflow
		return next_index - index - 1 < N;
	}

	T *get(size_t index)
	{
		return is_valid_index(index) ? &data[index % N] : nullptr;
	}

	const T *get(size_t index) const
	{
		return is_valid_index(index) ? &data[index % N] : nullptr;
	}

	bool set(size_t index, const T &value)
	{
		if (!is_valid_index(index))
			return false;

		data[index % N] = value;
		return true;
	}

	void add(const T &value)
	{
		data[next_index++ % N] = value;
	}

	size_t head_index(size_t offset = 0) const
	{
		return next_index - offset - 1;
	}

	T *head(size_t offset = 0)
	{
		if (offset >= stored())
			return nullptr;

		return get(head_index(offset));
	}

	const T *head(size_t offset = 0) const
	{
		if (offset >= stored())
			return nullptr;

		return get(head_index(offset));
	}

	size_t tail_index(size_t offset = 0) const
	{
		return next_index - stored() + offset;
	}

	T *tail(size_t offset = 0)
	{
		if (offset >= stored())
			return nullptr;

		return get(tail_index(offset));
	}

	const T *tail(size_t offset = 0) const
	{
		if (offset >= stored())
			return nullptr;

		return get(tail_index(offset));
	}
};