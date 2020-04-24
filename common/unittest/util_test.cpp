#include "s5utils.h"

#include <gtest/gtest.h>

TEST(S5Session, Iptest)
{
	const char* ip0 = "www.163.com";
	const char* ip1 = "192.168";
	const char* ip2 = "192.168.0";
	const char* ip3 = "256.168.0.1";
	const char* ip4 = "192.168.0.1";
	const char* ip5 = "255.255.255.255";

	ASSERT_EQ(FALSE, isIpValid(ip0));
	ASSERT_EQ(FALSE, isIpValid(ip1));
	ASSERT_EQ(FALSE, isIpValid(ip2));
	ASSERT_EQ(FALSE, isIpValid(ip3));
	ASSERT_EQ(TRUE, isIpValid(ip4));
	ASSERT_EQ(TRUE, isIpValid(ip5));
}

int main(int argc, char* argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

