#include <netdb.h> // used specifically for: struct sockaddr_in
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <unistd.h> // close(...);

struct request_line {
    char * method, * requestUri, * httpVersion;
};
struct key_value {
    char * key, * value;
    struct key_value * next;
};
struct http_request_header {
    struct request_line * reqline;
    struct key_value * fields;
};

void tryOrDie(int iserror, const char * errorOutput);
int initServer(unsigned short port);
char handleClient(int client_fd, struct sockaddr clientAddr);
struct http_request_header * doParse(const char * s);
struct key_value * getField(struct http_request_header * reqheader, const char * fieldName);
char iscrlf(const char * s);
void freeHttpReqHeader(struct http_request_header * reqheader);

int main(int argc, char * argv[]) {
    unsigned short port = 13370;
    if (argc > 1) port = atoi(argv[1]);
    else printf("no port was specified, using default port of %d\n", port);

    int socket_fd = initServer(port);
    while (1) {
        struct sockaddr clientAddr;
        socklen_t len = sizeof(clientAddr);
        int client_fd = accept(socket_fd, &clientAddr, &len);
        // int client_fd = accept(socket_fd, NULL, NULL);

        while (handleClient(client_fd, clientAddr) == 0);
    }

    close(socket_fd);
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
char handleClient(int client_fd, struct sockaddr clientAddr) {
    const int BUFFER_SIZE = 65535; int size = 0, readlen = 0, expectedLength = 0;
    char buffer[BUFFER_SIZE]; struct http_request_header * reqheader = NULL;

    while (expectedLength == 0 || size < expectedLength) {
        readlen = recv(client_fd, buffer + size, BUFFER_SIZE, 0);
        if (readlen <= 0) {
            printf("HEY GOT %d READLEN", readlen);
            exit(0);
        }
        size += readlen;

        if (reqheader == NULL)
        for (int i = 0; i + 3 < size; i++)
        if (iscrlf(buffer + i) && iscrlf(buffer + i + 2)) {
            reqheader = doParse(buffer);

            struct key_value * contentLength = getField(reqheader, "Content-Length");
            if (contentLength != NULL)  {
                int clen = atoi(contentLength->value);
                expectedLength = i + 4 + clen;
                if (expectedLength > 65535) { // maximum allowed length
                    close(client_fd); return 1;
                }
            } else expectedLength = size;
        }


    }

    printf("%s", buffer);

    const char * bodyTemplate =
        "<!doctype html><html><body>"
            "<pre>%s</pre>"
            "<pre style='color: red; padding-left: 1em;'>%s</pre>"
            "<style>pre { margin: 0; }</style>"
        "</body></html>";
    char * body = malloc(10000);

    sprintf(body, bodyTemplate, buffer, "SOME MORE TEXT HERE SOON");

    const char * responseTemplate = "HTTP/1.1 200 OK\r\n"
        "Content-Length: %d\r\n\r\n%s";
    char * response = malloc(10000);
    sprintf(response, responseTemplate, strlen(body), body);

    send(client_fd, response, strlen(response), 0);

    
    char closeConnection = 1;
    struct key_value * connection = getField(reqheader, "Connection");
    if (connection != NULL && strcmp(connection->value, "keep-alive") == 0) {
        closeConnection = 0;
    }

    freeHttpReqHeader(reqheader);
    if (closeConnection) close(client_fd);
    return closeConnection;
}
struct key_value * getField(struct http_request_header * reqheader, const char * fieldName) {
    for (struct key_value * f = reqheader->fields; f; f = f->next) {
        if (strcmp(f->key, fieldName) == 0) return f;
    } return NULL;
}
char iscrlf(const char * s) { return s[0] == '\r' && s[1] == '\n'; }
char readcrlf(const char ** s) { return iscrlf(*s) ? (*s)+=2, 0 : 1; } // 1 is failure
char * readToken(const char ** s) { // RFC2616:2.2
    const char * i = *s;
    while (**s >= 32 && **s <= 127 && !strchr("()<>@,;:\\\"/[]?={} \t", **s)) { (*s)++; }
    return i == *s ? NULL : strndup(i, *s - i);
}
char * readRequestUri(const char ** s) {
    if (**s == '*') return (*s)++, strndup(*s, 1);
    const char * i = *s;
    while (isalnum(**s) || strchr("-._~:/?#[]@!$&'()*+,;=%", **s)) (*s)++;
    return i == *s ? NULL : strndup(i, *s - i);
}
char * readHttpVersion(const char ** s) {
    const char * i = *s;
    if (i[0]!='H' || i[1]!='T' || i[2]!='T' || i[3]!='P' || i[4]!='/') return NULL;
    else (*s)+=5;
    if (isdigit(**s)) { (*s)++; while (isdigit(**s)) (*s)++; } else return NULL;
    if (**s=='.') (*s)++; else return NULL;
    if (isdigit(**s)) { (*s)++; while (isdigit(**s)) (*s)++; } else return NULL;
    return strndup(i, *s - i);
}
struct request_line * parseRequestLine(const char ** s) {
    #define ERROR_MSG "Request-Line is not properly formatted [RFC2616:5.1]"
    struct request_line * reqline = malloc(sizeof(struct request_line));
    memset(reqline, 0, sizeof(struct request_line));
    reqline->method = readToken(s);
    if (reqline->method == NULL || *((*s)++) != ' ') return printf(ERROR_MSG), free(reqline), NULL;
    reqline->requestUri = readRequestUri(s);
    if (reqline->requestUri == NULL || *((*s)++) != ' ') return printf(ERROR_MSG), free(reqline), NULL;
    reqline->httpVersion = readHttpVersion(s);
    if (reqline->httpVersion == NULL || readcrlf(s)) return printf(ERROR_MSG), free(reqline), NULL;
    return reqline;
    #undef ERROR_MSG
}
char * readText(const char ** s) {
    const char * i = *s;
    while (**s > 32 && **s < 127) (*s)++;
    return i == *s ? NULL : strndup(i, *s - i);
}
void skipws(const char ** s) {
    if (iscrlf(*s) && ((*s)[2]==' ' || (*s)[2]=='\t')) (*s)++;
    else if (**s==' ' || **s=='\t') (*s)++;
    else return;
    skipws(s);
}
char * parseFieldValue(const char ** s) {
    char * value = NULL, * text = NULL;
    while (1) {

        skipws(s);

        if (text = readText(s)) {

            int valuelen = value ? strlen(value) + 1 /* space character */ : 0;
            int newlen = valuelen + strlen(text) + 1 /* null-terminator*/;
            char * newvalue = malloc(newlen);
            memset(newvalue, 0, newlen);
            if (value) strcat(newvalue, value), strcat(newvalue, " "), free(value);

            strcat(newvalue, text), free(text);

            value = newvalue;


        } else break;
    }
    return skipws(s), value;
}
struct key_value * parseField(const char ** s) {
    #define ERROR_MSG "Field is not properly formatted [RFC2616:4.2]"
    struct key_value * field = malloc(sizeof(struct key_value));
    memset(field, 0, sizeof(struct key_value));

    field->key = readToken(s);

    if (field->key == NULL || *((*s)++) != ':') return printf(ERROR_MSG), free(field), NULL;
    field->value = parseFieldValue(s);

    if (field->key == NULL || !iscrlf(*s)) return printf(ERROR_MSG), free(field), NULL;

    return (*s) += 2, field;
    #undef ERROR_MSG
}
struct key_value * parseAllFields(const char ** s) {
    struct key_value * head = NULL, * tail = NULL, * field = NULL;

    while (!iscrlf(*s) && (field = parseField(s))) {

        if (head == NULL) head = tail = field;
        else tail->next = field, tail = field;
    }    
    return head;
}
struct http_request_header * doParse(const char * s) {
    const char * i = s;
    struct http_request_header * reqheader = malloc(sizeof(struct http_request_header));
    memset(reqheader, 0, sizeof(struct http_request_header));

    if ((reqheader->reqline = parseRequestLine(&s)) == NULL) return free(reqheader), NULL;


    reqheader->fields = parseAllFields(&s);

    if (!iscrlf(s)) return free(reqheader), NULL;

    return reqheader;
}
void freeHttpReqHeader(struct http_request_header * reqheader) {
    free(reqheader->reqline->method); free(reqheader->reqline->requestUri);
    free(reqheader->reqline->httpVersion); free(reqheader->reqline);    
    while (reqheader->fields) {
        struct key_value * next = reqheader->fields->next;
        free(reqheader->fields);
        reqheader->fields = next;
    }
    free(reqheader);
}