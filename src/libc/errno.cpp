int errno;

extern "C" int *__errno_location()
{
	return &errno;
}