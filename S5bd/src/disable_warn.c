#include "disable_warn.h"

#pragma GCC diagnostic ignored "-Wsign-conversion"

void s5_fd_set(int fd, fd_set *set)
{
	FD_SET(fd, set);
}

