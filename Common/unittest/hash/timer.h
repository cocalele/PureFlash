/// @file timer.h
/// @copyright BSD 2-clause. See LICENSE.txt for the complete license text.
/// @author Dane Larsen
/// @brief Two functions for profiling function call time.

#ifndef TIMER_H
#define TIMER_H

#include <time.h>

/// @brief A wrapper for getting the current time.
/// @returns The current time.
struct timespec snap_time()
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return t;
}

/// @brief Calculates the time difference between two struct timespecs
/// @param t1 The first time.
/// @param t2 The second time.
/// @returns The difference between the two times.
double get_elapsed(struct timespec t1, struct timespec t2)
{
    double ft1 = t1.tv_sec + ((double)t1.tv_nsec / 1000000000.0);
    double ft2 = t2.tv_sec + ((double)t2.tv_nsec / 1000000000.0);
    return ft2 - ft1;
}


#endif
