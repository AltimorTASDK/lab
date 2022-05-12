extern "C" int HSD_Rand();

extern "C" int rand_i()
{
	return HSD_Rand();
}