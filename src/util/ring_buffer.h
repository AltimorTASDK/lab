#pragma once

template<typename T, size_t N>
class ring_buffer {
	T data[N];
	size_t next_index = 0;

public:
	size_t capacity() const
	{
		return N;
	}

	size_t count() const
	{
		return next_index;
	}

	bool is_valid_index(size_t index) const
	{
		// Relies on unsigned overflow
		return next_index - index - 1 < N;
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

	const T *head(size_t offset = 0) const
	{
		if (offset >= next_index)
			return nullptr;

		return get(next_index - offset - 1);
	}
};