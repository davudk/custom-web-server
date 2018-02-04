#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char *argv[]) {

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
    addr.sin_port = htons(atoi(argv[1])); // HTTP is 80
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // any local IP
    int status = bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
    printf("(status: %s)\n", status ? "FAILURE" : "SUCCESS");
    if (status) return status;

    printf("listening... ");
    int queueLimit = 10;
    status = listen(socket_fd, queueLimit);
    printf("(status: %s)\n", status ? "FAILURE" : "SUCCESS");
    if (status) return status;

    printf("\n");
    while (1) {
        int client_fd = accept(socket_fd, NULL, NULL);

        const int BUFFER_SIZE = 70000;
        char buffer[BUFFER_SIZE];
        int size, readlen;
        
        char* template = "HTTP/1.0 200 OK\r\n"
            "Date: Sun, 04 Feb 2018 13:25:32 GMT\r\n"
            "Expires: -1\r\n"
            "Cache-Control: private, max-age=0\r\n"
            "Content-Type: text/html; charset=UTF-8\r\n"
            "Connection: close\r\n"
            "\r\n"
            "<!doctype html><html><body><pre>\r\n";
        send(client_fd, template, strlen(template), 0);

        while (readlen = recv(client_fd, buffer + size, BUFFER_SIZE, 0)) {
            if (readlen < 0) {
                printf("readlen < 0..");
                return -1;
            }

            send(client_fd, buffer + size, readlen, 0);
            size += readlen;

            // if (count > 65535) { // max limit for HTTP req
            //     break;
            // }
            break;
        }
        
        char* end = "</pre></body></html>";
        send(client_fd, end, strlen(end), 0);

        printf("%s", buffer);

        close(client_fd);
    }

    close(socket_fd);
}