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

struct uri_parts {
    char * host, * port, * path;
};
struct request_line {
    char * method, * requestUri, * httpVersion;
    struct uri_parts * uriParts;
};
struct key_value {
    char * key, * value;
    struct key_value * next;
};
struct http_request_header {
    struct request_line * reqline;
    struct key_value * fields;
};
struct client_connection {
    int client_fd;
    char buffer[BUFFER_SIZE + 1];
    ssize_t size, expectedsize;
    time_t lastReceived;
    struct http_request_header * header;
};

struct client_connection * clients[CLIENTS_MAX];

void tryOrDie(int iserror, const char * errorOutput);
int initServer(unsigned short port);
char handleClient(struct client_connection * c);
struct key_value * getField(struct http_request_header * reqheader, const char * fieldName);
struct uri_parts * parseUri(const char * uri);
char iscrlf(const char * s);
struct http_request_header * doParse(const char * s);
void freeHttpReqHeader(struct http_request_header * h);
char * doLookup(const char * host, const char * port);

int main(int argc, char * argv[]) {
    unsigned short port = 13370;
    if (argc > 1) tryOrDie((port = atoi(argv[1])) == 0, "Invalid port.\n");
    else printf("No port was specified... using default port: %d\n", port);

    int socket_fd = initServer(port);
    fcntl(socket_fd, F_SETFL, O_NONBLOCK);
    printf("stage 1 program by (ma995) listening on port (%d)\n", port);
    while (1) {
        int client_fd = accept(socket_fd, NULL, NULL), i = 0, found = 0;
        if (client_fd >= 0) {
            fcntl(client_fd, F_SETFL, O_NONBLOCK);
            for (i = 0; i < CLIENTS_MAX; i++) {
                if (clients[i]) continue;
                clients[i] = malloc(sizeof(struct client_connection));
                memset(clients[i], 0, sizeof(struct client_connection));
                clients[i]->client_fd = client_fd;
                clients[i]->lastReceived = time(NULL);
                found = 1; break;
            }
            if (found) printf("CONNECTED: fd=%d\n", client_fd);
            else close(client_fd), printf("REJECTED: reached max 32 fds\n");
        }
        for (i = 0; i < CLIENTS_MAX; i++) {
            if (!clients[i]) continue;
            if (handleClient(clients[i])) {
                printf("CLOSED:    fd=%d\n", clients[i]->client_fd);				
                if (clients[i]->header) freeHttpReqHeader(clients[i]->header);
                close(clients[i]->client_fd); free(clients[i]); clients[i] = NULL;
            }
        }
        nanosleep(&(struct timespec){ .tv_sec = 0, .tv_nsec = 1000000000L / 10 }, NULL);
    }
    // close(socket_fd); // never occurs anyways
}

void tryOrDie(int iserror, const char * errorOutput) {
    if (iserror) printf("%s", errorOutput), exit(EXIT_FAILURE);
}
int initServer(unsigned short port) {
    int domain = PF_INET; // IPv4
    int type = SOCK_STREAM; // TCP
    int protocol = 0; // default
    int socket_fd = socket(domain, type, protocol);
    tryOrDie(socket_fd < 0, "Failed to create socket...\n");
    
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; // IPv4
    addr.sin_port = htons(port); // HTTP is 80
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // any local IP
    int status = bind(socket_fd, (struct sockaddr *)&addr, sizeof(addr));
    tryOrDie(status < 0, "Failed to bind socket...\n");

    int queueLimit = 10;
    status = listen(socket_fd, queueLimit);
    tryOrDie(status < 0, "Failed to listen on port...\n");

    return socket_fd;
}
char handleClient(struct client_connection * c) {
    ssize_t count = recv(c->client_fd, c->buffer + c->size, BUFFER_SIZE, 0);
    if (count <= 0) return difftime(time(NULL), c->lastReceived) > TIMEOUT_SECS;
    c->size += count; c->buffer[c->size] = 0;
    c->lastReceived = time(NULL);
    if (c->header == NULL) {
        size_t contentIndex = 0, i;
        for (i = 0; i + 3 < c->size; i++)
            if (iscrlf(c->buffer + i) && iscrlf(c->buffer + i + 2)) 
                { contentIndex = i + 4; break; }
        if (contentIndex && (c->header = doParse(c->buffer))) {
            struct key_value * contentLength = getField(c->header, "Content-Length");
            if (contentLength)  {
                c->expectedsize = contentIndex + atoi(contentLength->value);
                if ((c->expectedsize = contentIndex + atoi(contentLength->value)) > BUFFER_SIZE) return 1; // maximum allowed length
            } else c->expectedsize = c->size;
        }
    }
    if (c->size >= c->expectedsize) {
        printf("REQUEST:   fd=%d    %s\n", c->client_fd, c->header->reqline->requestUri);

        char * hostip = doLookup(c->header->reqline->uriParts->host, c->header->reqline->uriParts->port);
        if (!hostip) hostip = "failed to resolve IP";

        const char * contentTemplate =
            "<!doctype html><html><body>"
                "<pre>%s</pre><pre style='color: red; margin: 1em;'>"
                "HOSTIP = %s (%s)\nPORT   = %s\nPATH   = %s</pre>"
            "</body></html>";
        char * content = malloc(10240);
        sprintf(content, contentTemplate, c->buffer, c->header->reqline->uriParts->host, hostip,
            c->header->reqline->uriParts->port, c->header->reqline->uriParts->path);

        const char * responseTemplate = "HTTP/1.1 200 OK\r\n"
            "Content-Length: %d\r\n"
            "Connection: keep-alive\r\n\r\n%s";
        char * response = malloc(10000);
        sprintf(response, responseTemplate, strlen(content), content);

        send(c->client_fd, response, strlen(response), 0); free(content); free(response);
        memset(c->buffer, 0, BUFFER_SIZE); c->size = c->expectedsize = 0;

        struct key_value * connection = getField(c->header, "Connection");
        char keepalive = strcasecmp(c->header->reqline->httpVersion, "HTTP/1.1") == 0 || // HTTP/1.1 keep alive by default
            (connection && strcasecmp(connection->value, "keep-alive") == 0);
        
        c->header = NULL;
        return !keepalive;
    }
    // keep alive until all data is received
    return 0; }
struct key_value * getField(struct http_request_header * reqheader, const char * fieldName) {
    struct key_value * f;
    for (f = reqheader->fields; f; f = f->next) {
        if (strcasecmp(f->key, fieldName) == 0) return f;
    } return NULL; }
struct uri_parts * parseUri(const char * uri) {
    struct uri_parts * p = malloc(sizeof(struct uri_parts));
    p->host = NULL; p->port = "80"; p->path = NULL;
    if (strncmp(uri, "http://", 7) == 0) uri += 7;
    const char * i = uri;
    while (isalnum(*i) || strchr("-.", *i)) i++;
    p->host = strndup(uri, i - uri);
    if (*i == ':') { i++; uri = i;
        while (isdigit(*i)) i++; p->port = strndup(uri, i - uri); }
    p->path = strdup(i);
    return p; }
char iscrlf(const char * s) { return s[0] == '\r' && s[1] == '\n'; }
char readcrlf(const char ** s) { return iscrlf(*s) ? (*s)+=2, 0 : 1; } // 1 is failure
char * readToken(const char ** s) { // RFC2616:2.2
    const char * i = *s;
    while (**s >= 32 && **s <= 127 && !strchr("()<>@,;:\\\"/[]?={} \t", **s)) { (*s)++; }
    return i == *s ? NULL : strndup(i, *s - i); }
char * readRequestUri(const char ** s) {
    if (**s == '*') return (*s)++, strndup(*s, 1);
    const char * i = *s;
    while (isalnum(**s) || strchr("-._~:/?#[]@!$&'()*+,;=%", **s)) (*s)++;
    return i == *s ? NULL : strndup(i, *s - i); }
char * readHttpVersion(const char ** s) {
    const char * i = *s;
    if (i[0]!='H' || i[1]!='T' || i[2]!='T' || i[3]!='P' || i[4]!='/') return NULL;
    else (*s)+=5;
    if (isdigit(**s)) { (*s)++; while (isdigit(**s)) (*s)++; } else return NULL;
    if (**s=='.') (*s)++; else return NULL;
    if (isdigit(**s)) { (*s)++; while (isdigit(**s)) (*s)++; } else return NULL;
    return strndup(i, *s - i); }
struct request_line * parseRequestLine(const char ** s) {
    const char * const ERROR_MSG = "Request-Line is not properly formatted [RFC2616:5.1]\n";
    struct request_line * reqline = malloc(sizeof(struct request_line));
    memset(reqline, 0, sizeof(struct request_line));
    reqline->method = readToken(s);
    if (reqline->method == NULL || *((*s)++) != ' ') return printf(ERROR_MSG), free(reqline), NULL;
    reqline->requestUri = readRequestUri(s);
    if (reqline->requestUri == NULL || *((*s)++) != ' ') return printf(ERROR_MSG), free(reqline->method), free(reqline), NULL;
    reqline->uriParts = parseUri(reqline->requestUri);
    if (reqline->uriParts == NULL) return printf(ERROR_MSG), free(reqline->method), free(reqline->requestUri), free(reqline), NULL;
    reqline->httpVersion = readHttpVersion(s);
    if (reqline->httpVersion == NULL || readcrlf(s)) return printf(ERROR_MSG), free(reqline->method), free(reqline->requestUri),
        free(reqline->uriParts->host), free(reqline->uriParts->port), free(reqline->uriParts->path), free(reqline->uriParts), free(reqline), NULL;
    return reqline; }
char * readText(const char ** s) {
    const char * i = *s;
    while (**s > 32 && **s < 127) (*s)++;
    return i == *s ? NULL : strndup(i, *s - i); }
void skipws(const char ** s) {
    if (iscrlf(*s) && ((*s)[2]==' ' || (*s)[2]=='\t')) (*s)++;
    else if (**s==' ' || **s=='\t') (*s)++;
    else return;
    skipws(s); }
char * parseFieldValue(const char ** s) {
    char * value = NULL, * text = NULL;
    while (1) {
        skipws(s);
        if (text = readText(s)) {
            int valuelen = value ? strlen(value) + 1 /* space character */ : 0;
            int newlen = valuelen + strlen(text) + 1 /* null-terminator */;
            char * newvalue = malloc(newlen);
            memset(newvalue, 0, newlen);
            if (value) strcat(newvalue, value), strcat(newvalue, " "), free(value);

            strcat(newvalue, text), free(text);

            value = newvalue;
        } else break; }
    return skipws(s), value; }
struct key_value * parseField(const char ** s) {
    const char * const ERROR_MSG = "Field is not properly formatted [RFC2616:4.2]\n";
    struct key_value * field = malloc(sizeof(struct key_value));
    memset(field, 0, sizeof(struct key_value));
    field->key = readToken(s);
    if (field->key == NULL || *((*s)++) != ':') return printf(ERROR_MSG), free(field), NULL;
    field->value = parseFieldValue(s);
    if (field->key == NULL || !iscrlf(*s)) return printf(ERROR_MSG), free(field), NULL;
    return (*s) += 2, field; }
struct key_value * parseAllFields(const char ** s) {
    struct key_value * head = NULL, * tail = NULL, * field = NULL;
    while (!iscrlf(*s) && (field = parseField(s))) {
        if (head == NULL) head = tail = field;
        else tail->next = field, tail = field; }
    return head; }
struct http_request_header * doParse(const char * s) {
    const char * i = s;
    struct http_request_header * reqheader = malloc(sizeof(struct http_request_header));
    memset(reqheader, 0, sizeof(struct http_request_header));
    if ((reqheader->reqline = parseRequestLine(&s)) == NULL) return free(reqheader), NULL;
    reqheader->fields = parseAllFields(&s);
    if (!iscrlf(s)) return free(reqheader), NULL;
    return reqheader; }
void freeHttpReqHeader(struct http_request_header * h) {
    free(h->reqline->method); free(h->reqline->requestUri);
    free(h->reqline->httpVersion); free(h->reqline->uriParts->host);
    free(h->reqline->uriParts->port); free(h->reqline->uriParts->path); free(h->reqline);
    while (h->fields) {
        struct key_value * next = h->fields->next;
        free(h->fields);
        h->fields = next; }
    free(h); }
char * doLookup(const char * host, const char * port) {	
    struct addrinfo * res, * rp;
    if (!getaddrinfo(host, port, NULL, &res)) {
        for (rp = res; rp; rp = rp->ai_next) {
            int resolve_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (resolve_fd < 0) continue;
            fcntl(resolve_fd, F_SETFL, O_NONBLOCK);
            if (connect(resolve_fd, rp->ai_addr, rp->ai_addrlen) >= 0) {
                char * hostip = inet_ntoa(((struct sockaddr_in *)rp->ai_addr)->sin_addr);
                close(resolve_fd);
                return hostip; } }
        freeaddrinfo(res); }
    return NULL; }