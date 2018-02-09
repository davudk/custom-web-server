#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 65535
#define TIMEOUT_SECS 30
#define CLIENTS_MAX 31 // max 32 fd's (32=31+1) ; 1 is server socket fd

struct key_value {
    char *key, *value;
    struct key_value *next;
};
struct http_request_header {
    char *method, *uri, *httpVersion, *uriHost, *uriPort, *uriPath;
    struct key_value *fields;
};
struct client_connection {
    int client_fd;
    char buffer[BUFFER_SIZE + 1];
    ssize_t size, expectedsize;
    time_t lastReceived;
    struct http_request_header *header;
};

struct client_connection *clients[CLIENTS_MAX];
char contentBuffer[70000], responseBuffer[80000];

void tryOrDie(int iserror, const char *errorOutput);
int initServer(unsigned short port);
char handleClient(struct client_connection *c);
struct key_value *getField(struct http_request_header *reqheader, const char *fieldName);
struct uri_parts *parseUri(const char *uri);
char iscrlf(const char *s);
struct http_request_header *doParse(const char *s);
void freeHttpReqHeader(struct http_request_header *h);
char *doLookup(const char *host, const char *port);

int main(int argc, char *argv[]) {
    unsigned short port = 1337;
    if (argc > 1) tryOrDie((port = atoi(argv[1])) == 0, "Invalid port.\n");
    else printf("usage: %s port\n", argv[0]), exit(EXIT_FAILURE);

    int socket_fd = initServer(port);
    fcntl(socket_fd, F_SETFL, O_NONBLOCK);
    printf("stage 1 program by (ma995) listening on port (%d)\n", port);
    while (1) {
        int client_fd = accept(socket_fd, NULL, NULL), i = 0, found = 0;
        if (client_fd >= 0)  {
            fcntl(client_fd, F_SETFL, O_NONBLOCK);
            for (i = 0; i < CLIENTS_MAX; i++) {
                if (clients[i]) continue;
                clients[i] = malloc(sizeof(struct client_connection));
                memset(clients[i], 0, sizeof(struct client_connection));
                clients[i]->client_fd = client_fd;
                clients[i]->lastReceived = time(NULL);
                found = 1; break;
            }
            if (found) printf("CONNECTED: fd=%2d\n", client_fd);
            else close(client_fd), printf("REJECTED: reached max 32 fds\n");
        }
        for (i = 0; i < CLIENTS_MAX; i++) {
            if (!clients[i]) continue;
            if (handleClient(clients[i])) {
                printf("CLOSED:    fd=%2d\n", clients[i]->client_fd);
                if (clients[i]->header) freeHttpReqHeader(clients[i]->header);
                close(clients[i]->client_fd);
                free(clients[i]);
                clients[i] = NULL;
            }
        }
        nanosleep(&(struct timespec){.tv_sec = 0, .tv_nsec = 1000000000L / 10}, NULL);
    }
    close(socket_fd); // never occurs anyways
}

void tryOrDie(int iserror, const char *errorOutput) {
    if (iserror) printf("%s", errorOutput), exit(EXIT_FAILURE);
}
int initServer(unsigned short port) {
    int socket_fd = socket(PF_INET /* IPv4 */, SOCK_STREAM /* TCP */, 0 /* default protocol */);
    tryOrDie(socket_fd < 0, "Failed to create socket...\n");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;                // IPv4
    addr.sin_port = htons(port);              // HTTP is 80
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // any local IP
    int status = bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
    tryOrDie(status < 0, "Failed to bind socket...\n");

    int queueLimit = 10;
    status = listen(socket_fd, queueLimit);
    tryOrDie(status < 0, "Failed to listen on port...\n");    
    return socket_fd;
}
char handleClient(struct client_connection *c) {
    ssize_t count = recv(c->client_fd, c->buffer + c->size, BUFFER_SIZE, 0);
    if (count <= 0) return difftime(time(NULL), c->lastReceived) > TIMEOUT_SECS;
    c->size += count; c->buffer[c->size] = 0; c->lastReceived = time(NULL);
    if (c->header == NULL) {
        size_t contentIndex = 0, i;
        for (i = 0; i + 3 < c->size; i++)
            if (iscrlf(c->buffer + i) && iscrlf(c->buffer + i + 2)) {
                contentIndex = i + 4; break;
            }
        if (contentIndex && (c->header = doParse(c->buffer))) {
            struct key_value *contentLength = getField(c->header, "Content-Length");
            if (contentLength) {
                c->expectedsize = contentIndex + atoi(contentLength->value);
                if ((c->expectedsize = contentIndex + atoi(contentLength->value)) > BUFFER_SIZE)
                    return 1; // maximum allowed length
            } else c->expectedsize = c->size;
        }
    }
    if (c->size >= c->expectedsize) {
        printf("REQUEST:   fd=%2d    %s\n", c->client_fd, c->header->uri);
        const char *hostip = doLookup(c->header->uriHost, c->header->uriPort),
            *contentTemplate = "<!doctype html><html><body>"
                "<pre>%s</pre><pre style='color: red; margin: 1em;'>"
                "HOSTIP = %s (%s)\nPORT   = %s\nPATH   = %s</pre>"
                "</body></html>",
            *responseTemplate = "HTTP/1.1 200 OK\r\n"
                "Content-Length: %d\r\n"
                "Connection: keep-alive\r\n"
                "Content-Type: text/html; charset=UTF-8\r\n\r\n%s";
        if (!hostip) hostip = "failed to resolve IP";
        sprintf(contentBuffer, contentTemplate, c->buffer, c->header->uriHost, hostip,
                c->header->uriPort, c->header->uriPath);
        sprintf(responseBuffer, responseTemplate, strlen(contentBuffer), contentBuffer);
        send(c->client_fd, responseBuffer, strlen(responseBuffer), 0);
        memset(c->buffer, 0, BUFFER_SIZE); c->size = c->expectedsize = 0;

        struct key_value *connection = getField(c->header, "Connection");
        char keepalive = strcasecmp(c->header->httpVersion, "HTTP/1.1") == 0 || // HTTP/1.1 keep alive by default
                         (connection && strcasecmp(connection->value, "keep-alive") == 0);
        return c->header = NULL, !keepalive;
    } return 0; // keep alive until all data is received
}
struct key_value *getField(struct http_request_header *reqheader, const char *fieldName) {
    struct key_value *f;
    for (f = reqheader->fields; f; f = f->next) {
        if (strcasecmp(f->key, fieldName) == 0) return f;
    } return NULL;
}
char iscrlf(const char *s) { return s[0] == '\r' && s[1] == '\n'; }
char * readToken(const char ** s) {
    const char * i = *s;
    while (**s >= 32 && **s <= 127 && !strchr("()<>@,;:\\\"/[]?={} \t", **s)) { (*s)++; }
    return i == *s ? NULL : strndup(i, *s - i);
}
char *readText(const char **s) {
    const char *i = *s;
    while (**s > 32 && **s < 127) (*s)++;
    return i == *s ? NULL : strndup(i, *s - i);
}
char parseRequestLine(const char **s, struct http_request_header *h) {
    const char *const ERROR_MSG = "Request-Line is not properly formatted [RFC2616:5.1]\n";
    if ((h->method = readText(s)) == NULL || *((*s)++) != ' ') return printf(ERROR_MSG), 1;
    if ((h->uri = readText(s)) == NULL || *((*s)++) != ' ') return printf(ERROR_MSG), 1;
    if ((h->httpVersion = readText(s)) == NULL || !iscrlf(*s)) return printf(ERROR_MSG), 1;
    (*s) += 2; const char *i = h->uri, *j = h->uri;
    if (strncmp(h->uri, "http://", 7) == 0) i += 7, j += 7;
    while (isalnum(*i) || strchr("-.", *i)) i++;
    h->uriHost = strndup(j, i - j);
    if (*i == ':') { i++; j = i;
        while (isdigit(*i)) i++; h->uriPort = strndup(j, i - j);
    } else h->uriPort = strdup("80");
    return h->uriPath = strdup(i), 0;
}
void skipws(const char **s) {
    if (iscrlf(*s) && ((*s)[2] == ' ' || (*s)[2] == '\t')) (*s)+=3;
    else if (**s == ' ' || **s == '\t') (*s)++;
    else return; skipws(s);
}
char *parseFieldValue(const char **s) {
    char *value = NULL, *text = NULL;
    while (1) {
        if (skipws(s), text = readText(s)) {
            int newlen = (value ? strlen(value) + 1 /* space */ : 0) + strlen(text) + 1 /* null */;
            char *newvalue = calloc(1, newlen);
            if (value) strcat(newvalue, value), strcat(newvalue, " "), free(value);
            value = strcat(newvalue, text), free(text), newvalue;
        } else break;
    } return value;
}
struct key_value *parseField(const char **s) {
    const char *const ERROR_MSG = "Field is not properly formatted [RFC2616:4.2]\n";
    struct key_value *field = calloc(1, sizeof(struct key_value));
    if ((field->key = readToken(s)) == NULL || *((*s)++) != ':') return printf(ERROR_MSG), free(field), NULL;
    if ((field->key = parseFieldValue(s)) == NULL || !iscrlf(*s)) return printf(ERROR_MSG), free(field->key), free(field), NULL;
    return (*s) += 2, field;
}
struct key_value *parseAllFields(const char **s) {
    struct key_value *head = NULL, *tail = NULL, *field = NULL;
    while (!iscrlf(*s) && (field = parseField(s))) {
        if (head == NULL) head = tail = field;
        else tail->next = field, tail = field;
    } return head;
}
struct http_request_header *doParse(const char *s) {
    const char *i = s;
    struct http_request_header * h = calloc(1, sizeof(struct http_request_header));
    if (parseRequestLine(&s, h)) return freeHttpReqHeader(h), NULL;
    h->fields = parseAllFields(&s);
    if (!iscrlf(s)) return freeHttpReqHeader(h), NULL;
    else return h;
}
void freeHttpReqHeader(struct http_request_header *h) {
    if (h->method) free(h->method); if (h->uri) free(h->uri);
    if (h->httpVersion) free(h->httpVersion); if (h->uriHost) free(h->uriHost);
    if (h->uriPort) free(h->uriPort); if (h->uriPath) free(h->uriPath);
    while (h->fields) {
        struct key_value *next = h->fields->next;
        free(h->fields); h->fields = next;
    } free(h);
}
char *doLookup(const char *host, const char *port) {
    struct addrinfo *res, *rp;
    if (!getaddrinfo(host, port, NULL, &res)) {
        for (rp = res; rp; rp = rp->ai_next) {
            int resolve_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (resolve_fd < 0)
                continue;
            fcntl(resolve_fd, F_SETFL, O_NONBLOCK);
            if (connect(resolve_fd, rp->ai_addr, rp->ai_addrlen) >= 0) {
                char *hostip = inet_ntoa(((struct sockaddr_in *)rp->ai_addr)->sin_addr);
                close(resolve_fd);
                return hostip;
            }
        } freeaddrinfo(res);
    } return NULL;
}