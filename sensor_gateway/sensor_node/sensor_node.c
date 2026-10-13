#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>

typedef struct
{
    uint16_t sensor_id;
    double value;
    uint64_t timestamp;
} __attribute__((packed)) tbp_frame_t;

int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in serv_addr;
    tbp_frame_t frame;

    if (argc != 4)
    {
        fprintf(stderr,
                "Usage: %s <ip> <port> <sensor_id>\n",
                argv[0]);
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    connect(sockfd,
            (struct sockaddr *)&serv_addr,
            sizeof(serv_addr));

    while (1)
    {
        frame.sensor_id = atoi(argv[3]);
        frame.value = 20.0 + (rand() % 100) / 10.0;
        frame.timestamp = time(NULL);

        send(sockfd,
             &frame,
             sizeof(frame),
             0);

        sleep(1);
    }
}