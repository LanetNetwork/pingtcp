/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
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
	sigset_t pingtcp_newmask;
	sigset_t pingtcp_oldmask;

	memzero(&server_address, sizeof(struct sockaddr_in));
	memzero(&time_to_sleep, sizeof(struct timespec));
	memzero(&ping_time_start, sizeof(struct timespec));
	memzero(&ping_time_end, sizeof(struct timespec));
	memzero(&pingtcp_newmask, sizeof(sigset_t));
	memzero(&pingtcp_oldmask, sizeof(sigset_t));

	if (unlikely(sigemptyset(&pingtcp_newmask) != 0))
	{
		perror("sigemptyset");
		exit(EX_OSERR);
	}
	if (unlikely(sigaddset(&pingtcp_newmask, SIGTERM) != 0))
	{
		perror("sigaddset");
		exit(EX_OSERR);
	}
	if (unlikely(sigaddset(&pingtcp_newmask, SIGINT) != 0))
	{
		perror("sigaddset");
		exit(EX_OSERR);
	}
	if (unlikely(pthread_sigmask(SIG_BLOCK, &pingtcp_newmask, &pingtcp_oldmask) != 0))
	{
		perror("pthread_sigmask");
		exit(EX_OSERR);
	}

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
	{
		perror("inet_ntoa");
		exit(EX_OSERR);
	}

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

		res = sigtimedwait(&pingtcp_newmask, NULL, &time_to_sleep);
		if (likely(res == -1))
		{
			if (likely(errno == EAGAIN || errno == EAGAIN))
				continue;
			else
			{
				perror("sigtimedwait");
				exit(EX_OSERR);
			}
		} else
			break;
	}

	freeaddrinfo(server);

	if (unlikely(pthread_sigmask(SIG_UNBLOCK, &pingtcp_newmask, NULL) != 0))
	{
		perror("pthread_sigmask");
		exit(EX_OSERR);
	}

	exit(EX_OK);
}

