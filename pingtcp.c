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

#define APP_VERSION		"0.0.3"
#define APP_YEAR		"2015"
#define APP_HOLDER		"Lanet Network"
#define APP_PROGRAMMER	"Oleksandr Natalenko"
#define APP_EMAIL		"o.natalenko@lanet.ua"

#define FQDN_MAX_LENGTH	254

static void __usage(char* _argv0)
{
	inform("Usage: %s <host> <port> [-c attempts] [-i interval] [-t timeout] [--tor | -6]\n", basename(_argv0));
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
	const char* (*inet_ntop)(int, const void*, char*, socklen_t);
	int (*getnameinfo)(const struct sockaddr*, socklen_t, char*, socklen_t, char*, socklen_t, int);
	int (*connect)(int, const struct sockaddr*, socklen_t);
	int (*close)(int);

	*(void**)(&socket) = dlsym(NULL, "socket");
	*(void**)(&getaddrinfo) = dlsym(NULL, "getaddrinfo");
	*(void**)(&freeaddrinfo) = dlsym(NULL, "freeaddrinfo");
	*(void**)(&inet_ntop) = dlsym(NULL, "inet_ntop");
	*(void**)(&getnameinfo) = dlsym(NULL, "getnameinfo");
	*(void**)(&connect) = dlsym(NULL, "connect");
	*(void**)(&close) = dlsym(NULL, "close");

	int socket_fd = -1;
	int port = -1;
	int res;
	int arg_index = 1;
	int proto = PF_INET;
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
	char* dst = NULL;
	char ptr[FQDN_MAX_LENGTH];
	pfcq_net_address_t address;
	pfcq_net_host_t host;
	struct addrinfo* server = NULL;
	struct addrinfo hints;
	struct timespec time_to_sleep;
	struct timespec ping_time_start;
	struct timespec ping_time_end;
	struct timespec wall_time_start;
	struct timespec wall_time_end;
	struct timeval timeout;
	sigset_t pingtcp_newmask;
	sigset_t pingtcp_oldmask;
	void* torsocks_hd = NULL;

	pfcq_zero(&address, sizeof(pfcq_net_address_t));
	pfcq_zero(&host, sizeof(pfcq_net_host_t));
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
				limit = strtoul(argv[arg_index + 1], NULL, 10);
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
				time_to_sleep = __pfcq_ns_to_timespec(strtoul(argv[arg_index + 1], NULL, 10) * 1000000UL);
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
				timeout = __pfcq_us_to_timeval(strtoul(argv[arg_index + 1], NULL, 10) * 1000UL);
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
			stop("Unable to load libtorsocks");
torloaded:
			*(void**)(&socket) = dlsym(torsocks_hd, "socket");
			*(void**)(&getaddrinfo) = dlsym(torsocks_hd, "getaddrinfo");
			*(void**)(&freeaddrinfo) = dlsym(torsocks_hd, "freeaddrinfo");
			*(void**)(&inet_ntop) = dlsym(torsocks_hd, "inet_ntop");
			*(void**)(&getnameinfo) = dlsym(torsocks_hd, "getnameinfo");
			*(void**)(&connect) = dlsym(torsocks_hd, "connect");
			*(void**)(&close) = dlsym(torsocks_hd, "close");

			arg_index++;
			continue;
		}

		if (strcmp(argv[arg_index], "--ipv6") == 0 ||
			strcmp(argv[arg_index], "-6") == 0)
		{
			proto = PF_INET6;
			arg_index++;
			continue;
		}

		if (!dst)
		{
			dst = pfcq_strdup(argv[arg_index]);
			arg_index++;
			continue;
		}

		if (port == -1)
		{
			if (pfcq_isnumber(argv[arg_index]))
			{
				port = strtoul(argv[arg_index], NULL, 10);
				arg_index++;
				continue;
			}
		}

		arg_index++;
	}

	if (port == -1)
		stop("Wrong port specified");

	hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
	hints.ai_family = proto == PF_INET6 ? AF_INET6 : AF_INET;
	hints.ai_socktype = 0;

	if (unlikely(clock_gettime(CLOCK_MONOTONIC, &wall_time_start) == -1))
		panic("clock_gettime");

	for (;;)
	{
		attempt++;
		current_ptr = 0;

		socket_fd = socket(proto, SOCK_STREAM, 0);
		if (unlikely(socket_fd == -1))
			panic("socket");

		res = getaddrinfo(dst, NULL, &hints, &server);
		if (unlikely(res))
			stop(gai_strerror(res));
		switch (proto)
		{
			case PF_INET:
				if (unlikely(!inet_ntop(AF_INET, &((struct sockaddr_in*)server->ai_addr)->sin_addr, host.host4, INET_ADDRSTRLEN)))
					panic("inet_ntop");
				break;
			case PF_INET6:
				if (unlikely(!inet_ntop(AF_INET6, &((struct sockaddr_in6*)server->ai_addr)->sin6_addr, host.host6, INET6_ADDRSTRLEN)))
					panic("inet_ntop");
				break;
			default:
				panic("socket family");
				break;
		}

		if (unlikely(attempt == 1))
			printf("PINGTCP %s (%s:%d)\n", dst, proto == PF_INET6 ? host.host6 : host.host4, port);
		switch (proto)
		{
			case PF_INET:
				address.address4.sin_family = AF_INET;
				memcpy(&address.address4.sin_addr, &((struct sockaddr_in*)server->ai_addr)->sin_addr, sizeof(struct in_addr));
				address.address4.sin_port = htons(port);
				break;
			case PF_INET6:
				address.address6.sin6_family = AF_INET6;
				memcpy(&address.address6.sin6_addr, &((struct sockaddr_in6*)server->ai_addr)->sin6_addr, sizeof(struct in6_addr));
				address.address6.sin6_port = htons(port);
				break;
			default:
				panic("socket family");
				break;
		}

		freeaddrinfo(server);

		pfcq_zero(ptr, FQDN_MAX_LENGTH);
		switch (proto)
		{
			case PF_INET:
				if (likely(getnameinfo((const struct sockaddr* restrict)&address.address4, sizeof(struct sockaddr_in), ptr, FQDN_MAX_LENGTH, NULL, 0, NI_NAMEREQD) == 0))
					current_ptr = 1;
				break;
			case PF_INET6:
				if (likely(getnameinfo((const struct sockaddr* restrict)&address.address6, sizeof(struct sockaddr_in6), ptr, FQDN_MAX_LENGTH, NULL, 0, NI_NAMEREQD) == 0))
					current_ptr = 1;
				break;
			default:
				panic("socket family");
				break;
		}

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_start) == -1))
			panic("clock_gettime");

		if (unlikely(setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1))
			panic("setsockopt");
		if (unlikely(setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1))
			panic("setsockopt");

		switch (proto)
		{
			case PF_INET:
				res = connect(socket_fd, (struct sockaddr*)&address.address4, sizeof (struct sockaddr_in));
				break;
			case PF_INET6:
				res = connect(socket_fd, (struct sockaddr*)&address.address6, sizeof(struct sockaddr_in6));
				break;
			default:
				panic("socket family");
				break;
		}

		if (unlikely(close(socket_fd) == -1))
			panic("close");

		if (unlikely(clock_gettime(CLOCK_MONOTONIC, &ping_time_end) == -1))
			panic("clock_gettime");

		if (unlikely(res == 0))
		{
			time_to_ping = __pfcq_timespec_diff_ns(ping_time_start, ping_time_end);
			time_to_ping_ms = (double)time_to_ping / 1000000.0;

			printf("Handshaked with %s:%d (%s): attempt=%lu time=%1.3lf ms\n",
					current_ptr ? ptr : dst, port, proto == PF_INET6 ? host.host6 : host.host4, attempt, time_to_ping_ms);
			if (time_to_ping_ms > rtt_max)
				rtt_max = time_to_ping_ms;
			if (time_to_ping_ms < rtt_min)
				rtt_min = time_to_ping_ms;
			rtt_sum += time_to_ping_ms;
			rtt_sum_sqr += pow(time_to_ping_ms, 2.0);

			ok++;
		} else
		{
			printf("Unable to handshake with %s:%d (%s): attempt=%lu\n",
					current_ptr ? ptr : dst, port, proto == PF_INET6 ? host.host6 : host.host4, attempt);

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

	printf("\n--- %s:%d pingtcp statistics ---\n", dst, port);
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
		if (unlikely(dlclose(torsocks_hd) != 0))
			panic("dlclose");

	pfcq_free(dst);

	exit(EX_OK);
}

