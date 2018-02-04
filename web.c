#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("requires port argument");
        return -1;
    }
    printf("creating a socket... ");
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM; // TCP
    int protocol = 0; // default
    int socket_fd = socket(domain, type, protocol);
    printf("(socket file descriptor: %d)\n", socket_fd);
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    printf("binding... ");
    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(atoi(argv[1])); // 80 HTTP
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // any local IP
    int status = bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("(status: %s)\n", status ? "FAILURE" : "SUCCESS");
    if (status) return status;

    printf("listening... ");
    int queueLimit = 10;
    status = listen(socket_fd, queueLimit);
    printf("(status: %s)\n", status ? "FAILURE" : "SUCCESS");
    if (status) return status;

    while (1) {
        struct sockaddr clientAddr; // not used...
        int client_fd = accept(socket_fd, NULL, NULL);

        char buffer[1000];
        int count;
        
        while (count = recv(client_fd, buffer, 1000, 0)) {
            if (count < 0) {
                printf("count < 0..");
                return -1;
            }

            printf("%s", buffer);
        }
    }

    close(socket_fd);
}