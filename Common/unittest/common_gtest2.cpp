#include <gtest/gtest.h>
#include "s5log.h"

S5LOG_INIT("LogTest2");

TEST(LogTest, Basic2) 
{
	S5LOG_DEBUG("Debug with 1 int arg=%d", 1);
	S5LOG_INFO("Info with 1 int arg=%d, str arg=%s", 1, "'Hello world'");
	S5LOG_WARN("Warn with no args");
	S5LOG_ERROR("Error with no args");
	S5LOG_TRACE("Trace with no args");
}