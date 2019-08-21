#include <sstream>
#include <stdlib.h>
#include <string>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>		 // For inet_addr()
#include <netinet/in.h> 	 // For sockaddr_in
#include <string>
#include <iostream>
#include <string.h>
using namespace std;

void usageExit()
{
	printf("Usage: spy <ip> <port> <cmd> <var_name> [var_name] ...\n cmd can be 'list', 'read'\n");
	exit(1);
}

int main(int argc, const char **argv) 
{
	if(argc < 4)
		usageExit();

	int port = atoi(argv[2]);


	ostringstream oss;
	for(int i=3;i<argc;i++)
		oss<<argv[i]<<" ";


	int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in addr={0};

	addr.sin_family = AF_INET;       // Internet address
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	addr.sin_port = htons((uint16_t)port);     // Assign port in network byte order
	socklen_t addr_len=sizeof(addr);
	ssize_t rc = sendto(socket_fd, oss.str().c_str(), oss.str().length(), 0, (sockaddr*)&addr, addr_len);
	if(rc == -1)
	{
		fprintf(stderr, "Failed send cmd. errno: %d %s.\n", errno, strerror(errno));
		return errno;
	}
	char buf[4096];
	ssize_t len = recvfrom(socket_fd, buf, sizeof(buf)-1, 0, NULL, NULL);
	if(len == -1)
	{
		fprintf(stderr, "Failed receive reply. errno: %d %s.\n", errno, strerror(errno));
		return errno;
	}
	buf[len]=0;
	printf("%s\n", buf);
	return 0;

}

