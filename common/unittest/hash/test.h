/// @file test.h
/// @copyright BSD 2-clause. See LICENSE.txt for the complete license text.
/// @author Dane Larsen
/// @brief A _very_ simple set of functions for running tests



#ifndef TEST_H
#define TEST_H

#include <stdio.h>

static int SUCCESS_COUNT = 0;
static int FAIL_COUNT = 0;

#ifdef TEST

/// @brief Prints the successful test message and
///        increments the number of successful
///        tests
#define test_success(M, ...) { fprintf(stderr, "[SUCCESS] " M "\n", ##__VA_ARGS__); SUCCESS_COUNT += 1; }

/// @brief Prints the failed test message and
///        increments the number of failed
///        tests
#define test_fail(M, ...) { fprintf(stderr, "*** FAIL *** " M "\n\n", ##__VA_ARGS__); FAIL_COUNT += 1; }

/// @def test(A, M, ...)
/// @brief A convenient test function
/// @param A A boolean value,
///        true will cause the test to be successful,
///        false will cause the test to fail
/// @param M The message to print upon test completion (accepts format string)
/// @param ... variables for the format string
#define test(A, M, ...) if(A) { test_success(M, ##__VA_ARGS__); } else { test_fail(M, ##__VA_ARGS__); }

#else

#define test_success(M, ...)
#define test_fail(M, ...)
#define test(A, M, ...)

#endif //TEST

/// @brief Returns the number of tests that passed
int successes() { return SUCCESS_COUNT; }
/// @brief Returns the number of tests that failed
int failures() { return FAIL_COUNT; }

/// @brief Reports the results of all of the tests to stderr
/// @returns 0 if all tests passed, 1 otherwise
int report_results()
{
    fprintf(stderr, "Test results: [%d successes, %d failures]\n", SUCCESS_COUNT, FAIL_COUNT);

    if(FAIL_COUNT > 0)
        return FAIL_COUNT;
    else
        return 0;
}

#endif //TEST_H
