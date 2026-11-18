#include <stddef.h>
#include <stdlib.h>

static int failNextMalloc;
static int failNextCalloc;

void reverbscTestFailNextMalloc(void)
{
	failNextMalloc = 1;
}

void reverbscTestFailNextCalloc(void)
{
	failNextCalloc = 1;
}

void* reverbscTestMalloc(size_t size)
{
	if (failNextMalloc)
	{
		failNextMalloc = 0;
		return NULL;
	}
	return malloc(size);
}

void* reverbscTestCalloc(size_t count, size_t size)
{
	if (failNextCalloc)
	{
		failNextCalloc = 0;
		return NULL;
	}
	return calloc(count, size);
}

#define malloc reverbscTestMalloc
#define calloc reverbscTestCalloc
#include "../../../plugins/ReverbSC/base.c"
#undef calloc
#undef malloc
