/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

#include <arpa/inet.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char** argv)
{
	int opts;
	int socket_fd = -1;
	int port = -1;
	int res;
	uint64_t attempt = 0;
	time_t time_to_ping = 0;
	double time_to_ping_ms = 0;
	char* host = NULL;
	char* host_ip = NULL;
	struct addrinfo* server = NULL;
	struct addrinfo hints;
	struct sockaddr_in server_address;
	struct timespec time_to_sleep;
	struct timespec ping_time_start;
	struct timespec ping_time_end;

	memset(&server_address, 0, sizeof(struct sockaddr_in));

	struct option longopts[] = {
		{"host",	required_argument,	NULL, 'a'},
		{"port",	required_argument,	NULL, 'b'},
		{0, 0, 0, 0}
	};

	while ((opts = getopt_long(argc, argv, "ab", longopts, NULL)) != -1)
	{
		switch (opts)
		{
			case 'a':
				host = strdupa(optarg);
				break;
			case 'b':
				port = atoi(optarg);
				break;
			default:
				exit(EX_USAGE);
				break;
		}
	}

	if (port == -1 || host == NULL)
	{
		printf("Usage: %s --host=<host> --port=<port>\n", basename(argv[0]));
		exit(EX_USAGE);
	}
	
	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
	hints.ai_family = PF_INET;
	hints.ai_socktype = 0;
	res = getaddrinfo(host, NULL, &hints, &server);
	if (res)
	{
		fprintf(stderr, "%s\n", gai_strerror(res));
		exit(EX_OSERR);
	}
	host_ip = inet_ntoa(((struct sockaddr_in*)server->ai_addr)->sin_addr);

	printf("PINGTCP %s (%s)\n", host, host_ip);

	for (;;)
	{
		attempt++;

		socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (socket_fd == -1)
		{
			perror("socket");
			exit(EX_OSERR);
		}

		server_address.sin_family = AF_INET;
		memcpy(&server_address.sin_addr, &((struct sockaddr_in*)server->ai_addr)->sin_addr, sizeof(struct in_addr));
		server_address.sin_port = htons(port);

		time_to_sleep.tv_sec = 1;
		time_to_sleep.tv_nsec = 0;

		if (clock_gettime(CLOCK_MONOTONIC, &ping_time_start) == -1)
		{
			perror("clock_gettime");
			exit(EX_OSERR);
		}

		if (connect(socket_fd, (struct sockaddr*)&server_address, sizeof(struct sockaddr_in)) == -1)
		{
			perror("connect");
			exit(EX_SOFTWARE);
		}

		if (close(socket_fd) == -1)
		{
			perror("close");
			exit(EX_OSERR);
		}
	
		if (clock_gettime(CLOCK_MONOTONIC, &ping_time_end) == -1)
		{
			perror("clock_gettime");
			exit(EX_OSERR);
		}

		time_to_ping =
			(ping_time_end.tv_sec * 1000000000ULL + ping_time_end.tv_nsec) -
			(ping_time_start.tv_sec * 1000000000ULL + ping_time_start.tv_nsec);

		time_to_ping_ms = (double)time_to_ping / 1000000.0;
		printf("Handshaked with %s:%d (%s): attempt=%ju time=%1.3lf ms\n", host, port, host_ip, attempt, time_to_ping_ms);
	
		while (nanosleep(&time_to_sleep, &time_to_sleep) == -1 && errno == EINTR)
			continue;
	}

	freeaddrinfo(server);

	exit(EX_OK);
}

