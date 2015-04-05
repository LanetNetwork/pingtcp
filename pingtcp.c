/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:noexpandtab */

/*
 * pingtcp - small utility to measure TCP handshake time (torify-friendly)
 * Copyright (C) 2015 Lanet Network
 * Programmed by Oleksandr Natalenko <o.natalenko@lanet.ua>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <getopt.h>
#include <libgen.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pfcq.h>
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

static void __usage(char* _argv0)
{
	inform("Usage: %s <host> <port> [-c attempts] [-i interval] [-t timeout] [--tor]\n", basename(_argv0));
	exit(EX_USAGE);
}

static void __version(void)
{
	inform("pingtcp v%s\n", APP_VERSION);
	inform("© %s, %s\n", APP_YEAR, APP_HOLDER);
	inform("Programmed by %s <%s>\n", APP_PROGRAMMER, APP_EMAIL);
	exit(EX_USAGE);
}

int main(int argc, char** argv)
{
	int (*socket)(int, int, int);
	int (*getaddrinfo)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
	void (*freeaddrinfo)(struct addrinfo*);
	char* (*inet_ntoa)(struct in_addr);
	int (*getnameinfo)(const struct sockaddr*, socklen_t, char*, socklen_t, char*, socklen_t, int);
	int (*connect)(int, const struct sockaddr*, socklen_t);
	int (*close)(int);

	*(void**)(&socket) = dlsym(NULL, "socket");
	*(void**)(&getaddrinfo) = dlsym(NULL, "getaddrinfo");
	*(void**)(&freeaddrinfo) = dlsym(NULL, "freeaddrinfo");
	*(void**)(&inet_ntoa) = dlsym(NULL, "inet_ntoa");
	*(void**)(&getnameinfo) = dlsym(NULL, "getnameinfo");
	*(void**)(&connect) = dlsym(NULL, "connect");
	*(void**)(&close) = dlsym(NULL, "close");

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
	void* torsocks_hd = NULL;

	pfcq_zero(&server_address, sizeof(struct sockaddr_in));
	pfcq_zero(&time_to_sleep, sizeof(struct timespec));
	pfcq_zero(&ping_time_start, sizeof(struct timespec));
	pfcq_zero(&ping_time_end, sizeof(struct timespec));
	pfcq_zero(&pingtcp_newmask, sizeof(sigset_t));
	pfcq_zero(&pingtcp_oldmask, sizeof(sigset_t));
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	time_to_sleep.tv_sec = 1;
	time_to_sleep.tv_nsec = 0;

	if (unlikely(sigemptyset(&pingtcp_newmask) != 0))
		panic("sigemptyset");
	if (unlikely(sigaddset(&pingtcp_newmask, SIGTERM) != 0))
		panic("sigaddset");
	if (unlikely(sigaddset(&pingtcp_newmask, SIGINT) != 0))
		panic("sigaddset");
	if (unlikely(pthread_sigmask(SIG_BLOCK, &pingtcp_newmask, &pingtcp_oldmask) != 0))
		panic("pthread_sigmask");

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
			if (arg_index < argc - 1 && pfcq_isnumber(argv[arg_index + 1]))
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
			if (arg_index < argc - 1 && pfcq_isnumber(argv[arg_index + 1]))
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
			if (arg_index < argc - 1 && pfcq_isnumber(argv[arg_index + 1]))
			{
				timeout.tv_sec = atoi(argv[arg_index + 1]);
				arg_index += 2;
				continue;
			} else
				__usage(argv[0]);
		}

		if (strcmp(argv[arg_index], "--tor") == 0 ||
			strcmp(argv[arg_index], "-T") == 0)
		{
			torsocks_hd = dlopen("libtorsocks.so", RTLD_LAZY);
			if (torsocks_hd)
				goto torloaded;
			torsocks_hd = dlopen("/usr/lib/libtorsocks.so", RTLD_LAZY);
			if (torsocks_hd)
				goto torloaded;
			torsocks_hd = dlopen("/usr/lib64/libtorsocks.so", RTLD_LAZY);
			if (torsocks_hd)
				goto torloaded;
			torsocks_hd = dlopen("/usr/lib/torsocks/libtorsocks.so", RTLD_LAZY);
			if (torsocks_hd)
				goto torloaded;
			torsocks_hd = dlopen("/usr/lib64/torsocks/libtorsocks.so", RTLD_LAZY);
			if (torsocks_hd)
				goto torloaded;
			panic("torsocks");
torloaded:
			*(void**)(&socket) = dlsym(torsocks_hd, "socket");
			*(void**)(&getaddrinfo) = dlsym(torsocks_hd, "getaddrinfo");
			*(void**)(&freeaddrinfo) = dlsym(torsocks_hd, "freeaddrinfo");
			*(void**)(&inet_ntoa) = dlsym(torsocks_hd, "inet_ntoa");
			*(void**)(&getnameinfo) = dlsym(torsocks_hd, "getnameinfo");
			*(void**)(&connect) = dlsym(torsocks_hd, "connect");
			*(void**)(&close) = dlsym(torsocks_hd, "close");

			arg_index++;
		}

		if (!host)
		{
			host = strdupa(argv[arg_index]);
			arg_index++;
			continue;
		}

		if (port == -1)
		{
			if (pfcq_isnumber(argv[arg_index]))
			{
				port = atoi(argv[arg_index]);
				arg_index++;
				continue;
			}
		}

		arg_index++;
	}

	if (port == -1)
		panic("Wrong port specified");

	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
	hints.ai_family = PF_INET;
	hints.ai_socktype = 0;

	if (unlikely(clock_gettime(CLOCK_MONOTONIC, &wall_time_start) == -1))
		panic("clock_gettime");

	for (;;)
	{
		attempt++;
		current_ptr = 0;

		socket_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (unlikely(socket_fd == -1))
			panic("socket");

		res = getaddrinfo(host, NULL, &hints, &server);
		if (unlikely(res))
			panic(gai_strerror(res));
		host_ip = inet_ntoa(((struct sockaddr_in*)server->ai_addr)->sin_addr);
		if (unlikely(!host_ip))
			panic("inet_ntoa");

		if (unlikely(attempt == 1))
			printf("PINGTCP %s (%s:%d)\n", host, host_ip, port);
		server_address.sin_family = AF_INET;
		memcpy(&server_address.sin_addr, &((struct sockaddr_in*)server->ai_addr)->sin_addr, sizeof(struct in_addr));
		server_address.sin_port = htons(port);

		freeaddrinfo(server);

		pfcq_zero(ptr, FQDN_MAX_LENGTH);
		if (likely(getnameinfo((const struct sockaddr* restrict)&server_address, sizeof(struct sockaddr_in), ptr, FQDN_MAX_LENGTH, NULL, 0, NI_NAMEREQD) == 0))
			current_ptr = 1;

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_start) == -1))
			panic("clock_gettime");

		if (unlikely(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1))
			panic("setsockopt");
		if (unlikely(setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1))
			panic("setsockopt");

		res = connect(socket_fd, (struct sockaddr*)&server_address, sizeof(struct sockaddr_in));

		if (unlikely(close(socket_fd) == -1))
			panic("close");

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_end) == -1))
			panic("clock_gettime");

		if (unlikely(res == 0))
		{
			time_to_ping = __pfcq_timespec_diff_ns(ping_time_start, ping_time_end);
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
				panic("sigtimedwait");
		} else
			break;
	}

	if (unlikely(clock_gettime(CLOCK_MONOTONIC, &wall_time_end) == -1))
		panic("clock_gettime");

	if (unlikely(pthread_sigmask(SIG_UNBLOCK, &pingtcp_newmask, NULL) != 0))
		panic("pthread_sigmask");

	printf("\n--- %s:%d pingtcp statistics ---\n", host, port);
	loss = (double)fail / (double)attempt * 100.0;
	wall_time = __pfcq_timespec_diff_ns(wall_time_start, wall_time_end);
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

	if (torsocks_hd)
		dlclose(torsocks_hd);

	exit(EX_OK);
}

