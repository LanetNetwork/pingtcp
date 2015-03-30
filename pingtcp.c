/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

#include <arpa/inet.h>
#include <ctype.h>
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

#define memzero(A,B)	memset(A, 0, B)

#ifdef __GNUC__
#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)
#else /* __GNUC__ */
#define likely(x)		(x)
#define unlikely(x)		(x)
#endif /* __GNUC__ */

static int isnumber(const char* _string)
{
	while (likely(*_string))
	{
		char current_char = *_string++;
		if (unlikely(isdigit(current_char) == 0))
			return 0;
	}

	return 1;
}

int main(int argc, char** argv)
{
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

	memzero(&server_address, sizeof(struct sockaddr_in));

	if (argc < 3)
	{
		fprintf(stderr, "Usage: %s <host> <port>\n", basename(argv[0]));
		exit(EX_USAGE);
	}

	host = strdupa(argv[1]);
	if (isnumber(argv[2]))
		port = atoi(argv[2]);

	if (port == -1)
	{
		fprintf(stderr, "Wrong port specified\n");
		exit(EX_USAGE);
	}

	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
	hints.ai_family = PF_INET;
	hints.ai_socktype = 0;
	res = getaddrinfo(host, NULL, &hints, &server);
	if (unlikely(res))
	{
		fprintf(stderr, "%s\n", gai_strerror(res));
		exit(EX_OSERR);
	}
	host_ip = inet_ntoa(((struct sockaddr_in*)server->ai_addr)->sin_addr);
	if (unlikely(!host_ip))
		perror("inet_ntoa");

	printf("PINGTCP %s (%s)\n", host, host_ip);

	for (;;)
	{
		attempt++;

		socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (unlikely(socket_fd == -1))
		{
			perror("socket");
			exit(EX_OSERR);
		}

		server_address.sin_family = AF_INET;
		memcpy(&server_address.sin_addr, &((struct sockaddr_in*)server->ai_addr)->sin_addr, sizeof(struct in_addr));
		server_address.sin_port = htons(port);

		time_to_sleep.tv_sec = 1;
		time_to_sleep.tv_nsec = 0;

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_start) == -1))
		{
			perror("clock_gettime");
			exit(EX_OSERR);
		}

		if (unlikely(connect(socket_fd, (struct sockaddr*)&server_address, sizeof(struct sockaddr_in)) == -1))
		{
			perror("connect");
			exit(EX_SOFTWARE);
		}

		if (unlikely(close(socket_fd) == -1))
		{
			perror("close");
			exit(EX_OSERR);
		}

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_end) == -1))
		{
			perror("clock_gettime");
			exit(EX_OSERR);
		}

		time_to_ping =
			(ping_time_end.tv_sec * 1000000000ULL + ping_time_end.tv_nsec) -
			(ping_time_start.tv_sec * 1000000000ULL + ping_time_start.tv_nsec);

		time_to_ping_ms = (double)time_to_ping / 1000000.0;
		printf("Handshaked with %s:%d (%s): attempt=%lu time=%1.3lf ms\n", host, port, host_ip, attempt, time_to_ping_ms);

		while (likely(nanosleep(&time_to_sleep, &time_to_sleep) == -1 && errno == EINTR))
			continue;
	}

	freeaddrinfo(server);

	exit(EX_OK);
}

