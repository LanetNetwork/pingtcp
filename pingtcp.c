/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <getopt.h>
#include <libgen.h>
#include <math.h>
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

#define APP_VERSION		"0.0.2"
#define APP_YEAR		"2015"
#define APP_HOLDER		"Lanet Network"
#define APP_PROGRAMMER	"Oleksandr Natalenko"
#define APP_EMAIL		"o.natalenko@lanet.ua"

#define FQDN_MAX_LENGTH	254

#define memzero(A,B)	memset(A, 0, B)

#ifdef __GNUC__
#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)
#else /* __GNUC__ */
#define likely(x)		(x)
#define unlikely(x)		(x)
#endif /* __GNUC__ */

static void __usage(char* _argv0)
{
	fprintf(stderr, "Usage: %s <host> <port> [-c attempts] [-i interval] [-t timeout]\n", basename(_argv0));
	exit(EX_USAGE);
}

static void __version(void)
{
	fprintf(stderr, "pingtcp v%s\n", APP_VERSION);
	fprintf(stderr, "© %s, %s\n", APP_YEAR, APP_HOLDER);
	fprintf(stderr, "Programmed by %s <%s>\n", APP_PROGRAMMER, APP_EMAIL);
	exit(EX_USAGE);
}

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
	int arg_index = 1;
	uint64_t attempt = 0;
	uint64_t ok = 0;
	uint64_t fail = 0;
	uint64_t limit = 0;
	unsigned short int current_ptr = 0;
	time_t time_to_ping = 0;
	time_t wall_time = 0;
	double time_to_ping_ms = 0;
	double wall_time_ms = 0;
	double loss = 0;
	double rtt_min = DBL_MAX;
	double rtt_avg = 0;
	double rtt_max = DBL_MIN;
	double rtt_sum = 0;
	double rtt_sum_sqr = 0;
	double rtt_mdev = 0;
	char* host = NULL;
	char* host_ip = NULL;
	char ptr[FQDN_MAX_LENGTH];
	struct addrinfo* server = NULL;
	struct addrinfo hints;
	struct sockaddr_in server_address;
	struct timespec time_to_sleep;
	struct timespec ping_time_start;
	struct timespec ping_time_end;
	struct timespec wall_time_start;
	struct timespec wall_time_end;
	struct timeval timeout;
	sigset_t pingtcp_newmask;
	sigset_t pingtcp_oldmask;

	memzero(&server_address, sizeof(struct sockaddr_in));
	memzero(&time_to_sleep, sizeof(struct timespec));
	memzero(&ping_time_start, sizeof(struct timespec));
	memzero(&ping_time_end, sizeof(struct timespec));
	memzero(&pingtcp_newmask, sizeof(sigset_t));
	memzero(&pingtcp_oldmask, sizeof(sigset_t));
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	time_to_sleep.tv_sec = 1;
	time_to_sleep.tv_nsec = 0;

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

	if (argc < 2)
		__usage(argv[0]);

	while (arg_index < argc)
	{
		if (strcmp(argv[arg_index], "--help") == 0 ||
			strcmp(argv[arg_index], "-h") == 0)
			__usage(argv[0]);

		if (strcmp(argv[arg_index], "--version") == 0 ||
			strcmp(argv[arg_index], "-v") == 0)
			__version();

		if (strcmp(argv[arg_index], "--count") == 0 ||
			strcmp(argv[arg_index], "-c") == 0)
		{
			if (isnumber(argv[arg_index + 1]))
			{
				limit = atoi(argv[arg_index + 1]);
				arg_index += 2;
				continue;
			} else
				__usage(argv[0]);
		}

		if (strcmp(argv[arg_index], "--interval") == 0 ||
			strcmp(argv[arg_index], "-i") == 0)
		{
			if (isnumber(argv[arg_index + 1]))
			{
				time_to_sleep.tv_sec = atoi(argv[arg_index + 1]);
				arg_index += 2;
				continue;
			} else
				__usage(argv[0]);
		}

		if (strcmp(argv[arg_index], "--timeout") == 0 ||
			strcmp(argv[arg_index], "-t") == 0)
		{
			if (isnumber(argv[arg_index + 1]))
			{
				timeout.tv_sec = atoi(argv[arg_index + 1]);
				arg_index += 2;
				continue;
			} else
				__usage(argv[0]);
		}

		if (!host)
		{
			host = strdupa(argv[arg_index]);
			arg_index++;
			continue;
		}

		if (port == -1)
		{
			if (isnumber(argv[arg_index]))
			{
				port = atoi(argv[arg_index]);
				arg_index++;
				continue;
			}
		}

		arg_index++;
	}

	if (port == -1)
	{
		fprintf(stderr, "Wrong port specified\n");
		exit(EX_USAGE);
	}

	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
	hints.ai_family = PF_INET;
	hints.ai_socktype = 0;

	if (unlikely(clock_gettime(CLOCK_MONOTONIC, &wall_time_start) == -1))
	{
		perror("clock_gettime");
		exit(EX_OSERR);
	}

	for (;;)
	{
		attempt++;
		current_ptr = 0;

		socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (unlikely(socket_fd == -1))
		{
			perror("socket");
			exit(EX_OSERR);
		}

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

		if (unlikely(attempt == 1))
			printf("PINGTCP %s (%s:%d)\n", host, host_ip, port);
		server_address.sin_family = AF_INET;
		memcpy(&server_address.sin_addr, &((struct sockaddr_in*)server->ai_addr)->sin_addr, sizeof(struct in_addr));
		server_address.sin_port = htons(port);

		freeaddrinfo(server);

		memzero(ptr, FQDN_MAX_LENGTH);
		if (likely(getnameinfo((const struct sockaddr* restrict)&server_address, sizeof(struct sockaddr_in), ptr, FQDN_MAX_LENGTH, NULL, 0, NI_NAMEREQD) == 0))
			current_ptr = 1;

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_start) == -1))
		{
			perror("clock_gettime");
			exit(EX_OSERR);
		}

		if (unlikely(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1))
		{
			perror("setsockopt");
			exit(EX_OSERR);
		}
		if (unlikely(setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1))
		{
			perror("setsockopt");
			exit(EX_OSERR);
		}

		res = connect(socket_fd, (struct sockaddr*)&server_address, sizeof(struct sockaddr_in));

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

		if (unlikely(res == 0))
		{
			time_to_ping =
				(ping_time_end.tv_sec * 1000000000ULL + ping_time_end.tv_nsec) -
				(ping_time_start.tv_sec * 1000000000ULL + ping_time_start.tv_nsec);
			time_to_ping_ms = (double)time_to_ping / 1000000.0;

			printf("Handshaked with %s:%d (%s): attempt=%lu time=%1.3lf ms\n", current_ptr ? ptr : host, port, host_ip, attempt, time_to_ping_ms);
			if (time_to_ping_ms > rtt_max)
				rtt_max = time_to_ping_ms;
			if (time_to_ping_ms < rtt_min)
				rtt_min = time_to_ping_ms;
			rtt_sum += time_to_ping_ms;
			rtt_sum_sqr += pow(time_to_ping_ms, 2.0);

			ok++;
		} else
		{
			printf("Unable to handshake with %s:%d (%s): attempt=%lu\n", current_ptr ? ptr : host, port, host_ip, attempt);

			fail++;
		}

		if (limit != 0 && attempt + 1 > limit)
			break;

		res = sigtimedwait(&pingtcp_newmask, NULL, &time_to_sleep);
		if (likely(res == -1))
		{
			if (likely(errno == EAGAIN || errno == EINTR))
				continue;
			else
			{
				perror("sigtimedwait");
				exit(EX_OSERR);
			}
		} else
			break;
	}

	if (unlikely(clock_gettime(CLOCK_MONOTONIC, &wall_time_end) == -1))
	{
		perror("clock_gettime");
		exit(EX_OSERR);
	}

	if (unlikely(pthread_sigmask(SIG_UNBLOCK, &pingtcp_newmask, NULL) != 0))
	{
		perror("pthread_sigmask");
		exit(EX_OSERR);
	}

	printf("\n--- %s:%d pingtcp statistics ---\n", host, port);
	loss = (double)fail / (double)attempt * 100.0;
	wall_time =
			(wall_time_end.tv_sec * 1000000000ULL + wall_time_end.tv_nsec) -
			(wall_time_start.tv_sec * 1000000000ULL + wall_time_start.tv_nsec);
	wall_time_ms = (double)wall_time / 1000000.0;
	if (ok > 0)
	{
		rtt_avg = rtt_sum / ok;
		rtt_sum /= ok;
		rtt_sum_sqr /= ok;
		rtt_mdev = sqrt(rtt_sum_sqr - pow(rtt_sum, 2.0));
	} else
	{
		rtt_min = 0;
		rtt_max = 0;
	}
	printf("%lu handshake(s) started, %lu succeeded, %1.3lf%% loss, time %1.3lf ms\n", attempt, ok, loss, wall_time_ms);
	printf("rtt min/avg/max/mdev = %1.3lf/%1.3lf/%1.3lf/%1.3lf\n", rtt_min, rtt_avg, rtt_max, rtt_mdev);

	exit(EX_OK);
}

