#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

void tryOrDie(int iserror, const char * errorOutput);
int initServer(unsigned short port);
void handleClient(int client_fd);

int main(int argc, char *argv[]) {

    int socket_fd = initServer(atoi(argv[1]));

    printf("\n");
    while (1) {
        int client_fd = accept(socket_fd, NULL, NULL);
        fcntl(client_fd, F_SETFL, O_NONBLOCK);
        handleClient(client_fd);
        close(client_fd);
    }

    //close(socket_fd);
}

void tryOrDie(int iserror, const char * errorOutput) {
    if (iserror) printf("%s", errorOutput), exit(EXIT_FAILURE);
}

int initServer(unsigned short port) {
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM; // TCP
    int protocol = 0; // default
    int socket_fd = socket(domain, type, protocol);
    tryOrDie(socket_fd < 0, "failed to create socket\n");
    printf("created socket (fd: %d)\n", socket_fd);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(port); // HTTP is 80
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // any local IP
    //addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // localhost
    int status = bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
    tryOrDie(status < 0, "failed to bind socket\n");
    printf("binded socket\n");

    int queueLimit = 10;
    status = listen(socket_fd, queueLimit);
    tryOrDie(status < 0, "failed to listen on port\n");
    printf("listening on port %d\n\n", port);

    return socket_fd;
}

void handleClient(int client_fd) {
    const int BUFFER_SIZE = 70000;
    char buffer[BUFFER_SIZE];
    int size = 0, readlen;
    
    char* template = "HTTP/1.0 200 OK\r\n"
        "Date: Sun, 04 Feb 2018 13:25:32 GMT\r\n"
        "Expires: -1\r\n"
        "Cache-Control: private, max-age=0\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!doctype html><html><body><pre>\r\n";
    send(client_fd, template, strlen(template), 0);

    time_t startTime = time(NULL), now;
    while (readlen = recv(client_fd, buffer + size, BUFFER_SIZE, 0)) {
        printf("[read %d]", readlen);
        if (readlen == -1) {

            now = time(NULL);
            if (difftime(now, startTime) > 2) {
                printf("[break]");
                break;
            }

            struct timespec duration, actual;
            duration.tv_sec = 0;
            duration.tv_nsec = 1000000000L / 10;
            nanosleep(&duration, &actual);
            continue;
            //printf("\nreadlen < 0..\n");
            //return;
        }

        // received data, reset timer
        startTime = time(NULL);

        send(client_fd, buffer + size, readlen, 0);
        size += readlen;

        // if (count > 65535) { // max limit for HTTP req
        //     break;
        // }
    }
    
    char* end = "</pre></body></html>";
    send(client_fd, end, strlen(end), 0);

    printf("\n[total_read %d]\n", size);
    buffer[size] = 0;
    printf("%s", buffer);
}