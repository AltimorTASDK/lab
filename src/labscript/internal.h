#pragma once

namespace labscript {

template<typename T>
inline bool set_result(void *result, T value)
{
	if (result == nullptr)
		return false;

	*(T*)result = value;
	return true;
}

} // namespace labscript