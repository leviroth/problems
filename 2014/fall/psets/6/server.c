// feature test macro requirements
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED

// http://httpd.apache.org/docs/2.2/mod/core.html
#define LimitRequestBody 102400
#define LimitRequestFields 50
#define LimitRequestFieldSize 4094
#define LimitRequestLine 8190

// block size
#define BLOCK 512

// header files
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// types
typedef char OCTET;

// prototypes
bool connected(void);
void handler(int signal);
const char* lookup(const char* extension);
const OCTET* parse(void);
bool respond(unsigned short code, ...);
void reset(void);
void start(unsigned short port, const char* path);
void stop(void);

// server's root
char* root = NULL;

// file descriptor for sockets
int cfd = -1, sfd = -1;

// FILE pointer for files
FILE* file = NULL;

int main(int argc, char* argv[])
{
    // a global variable defined in errno.h that's "set by system 
    // calls and some library functions [to a nonzero value]
    // in the event of an error to indicate what went wrong"
    errno = 0;

    // default to a random port
    unsigned short port = 0;

    // usage
    const char* usage = "Usage: server [-p port] [-q] /path/to/root";

    // parse command-line arguments
    int opt;
    while ((opt = getopt(argc, argv, "hp:")) != -1)
    {
        switch (opt)
        {
            // -h
            case 'h':
                printf("%s\n", usage);
                return 0;

            // -p port
            case 'p':
                port = atoi(optarg);
                break;
        }
    }

    // ensure path to server's root was specified
    if (argv[optind] == NULL || strlen(argv[optind]) == 0)
    {
        // announce usage
        printf("%s\n", usage);

        // return 2 just like bash's builtins
        return 2;
    }

    // start server
    start(port, argv[optind]);

    // listen for SIGINT (aka control-c)
    signal(SIGINT, handler);

    // accept connections one at a time
    while (true)
    {
        // reset server's state
        reset();

        // wait until client is connected
        if (connected())
        {
            // parse client's HTTP request, omitting message-body
            const OCTET* request = parse();
            if (request == NULL)
            {
                respond(500);
                continue;
            }

            // extract request's Request-Line
            const char* haystack = request;
            char* needle = strstr(haystack, "\r\n");
            if (needle == NULL)
            {
                respond(400);
                continue;
            }
            else if (needle - haystack + 2 > LimitRequestLine)
            {
                respond(414);
                continue;
            }   
            char line[needle - haystack + 2 + 1];
            strncpy(line, haystack, needle - haystack + 2);
            line[needle - haystack + 2] = '\0';

            // log Request-Line
            printf("%s", line);

            // find first SP in Request-Line
            haystack = line;
            needle = strchr(haystack, ' ');
            if (needle == NULL)
            {
                respond(400);
                continue;
            }

            // extract Method
            char method[needle - haystack + 1];
            strncpy(method, haystack, needle - haystack);
            method[needle - haystack] = '\0';

            // find second SP in Request-Line
            haystack = needle + 1;
            needle = strchr(haystack, ' ');
            if (needle == NULL)
            {
                respond(400);
                continue;
            }

            // extract Request-URI
            char uri[needle - haystack + 1];
            strncpy(uri, haystack, needle - haystack);
            uri[needle - haystack] = '\0';

            // find first CRLF in Request-Line
            haystack = needle + 1;
            needle = strstr(haystack, "\r\n");
            if (needle == NULL)
            {
                respond(414);
                continue;
            }

            // extract Version
            char version[needle - haystack + 1];
            strncpy(version, haystack, needle - haystack);
            version[needle - haystack] = '\0';

            // ensure request's method is GET
            if (strcmp("GET", method) != 0)
            {
                respond(405);
                continue;
            }

            // ensure Request-URI starts with abs_path
            if (uri[0] != '/')
            {
                respond(501);
                continue;
            }

            // ensure request's version is HTTP/1.1
            if (strcmp("HTTP/1.1", version) != 0)
            {
                respond(505);
                continue;
            }

            // find end of abs_path in Request-URI
            haystack = uri;
            needle = strchr(haystack, '?');
            if (needle == NULL)
            {
                needle = uri + strlen(uri);
            }

            // extract abs_path
            char abs_path[needle - haystack + 1];
            strncpy(abs_path, uri, needle - haystack);
            abs_path[needle - haystack] = '\0';

            // find start of query in Request-URI
            if (*needle == '?')
            {
                needle = needle + 1;
            }

            // extract query
            char query[strlen(needle) + 1];
            strcpy(query, needle);

            // determine file's full path
            char path[strlen(root) + strlen(abs_path) + 1];
            strcpy(path, root);
            strcat(path, abs_path);

            // ensure file exists
            if (access(path, F_OK) == -1)
            {
                respond(404);
                continue;
            }

            // ensure file is readable
            if (access(path, R_OK) == -1)
            {
                respond(403);
                continue;
            }

            // extract file's extension
            haystack = path;
            needle = strrchr(haystack, '.');
            if (needle == NULL)
            {
                respond(501);
                continue;
            }
            char extension[strlen(needle)];
            strcpy(extension, needle + 1);

            // look up file's MIME type
            const char* type = lookup(extension);
            if (type == NULL)
            {
                respond(501);
                continue;
            }

            // open file
            file = fopen(path, "r");
            if (file == NULL)
            {
                respond(500);
                continue;
            }

            /*
            // determine file's length
            off_t length = lseek(fd, 0, SEEK_END);
            lseek(fd, 0, SEEK_SET);
            */

            /*
            // read file into buffer
            char buffer[length];
            ssize_t n;
            while ((n = read(fd, buffer, CAPACITY)) > 0) // TODO: check for error
            {
                write(cfd, buffer, n);
            }

            // respond to client
            respond(200, length, type, NULL);
            */
        }
    }
}

/**
 * Accepts a connection from a client, blocking (i.e., waiting) until one is heard.
 */
bool connected(void)
{
    struct sockaddr_in cli_addr;
    memset(&cli_addr, 0, sizeof(cli_addr));
    socklen_t cli_len = sizeof(cli_addr);
    cfd = accept(sfd, (struct sockaddr*) &cli_addr, &cli_len);
    if (cfd == -1)
    {
        return false;
    }
    return true;
}

/**
 * Handles signals.
 */
void handler(int signal)
{
    // control-c
    if (signal == SIGINT)
    {
        printf("Stopping server\n");
        stop();
    }
}

/**
 * Returns MIME type for supported extensions, else NULL.
 */
const char* lookup(const char* extension)
{
    // .css
    if (strcasecmp("css", extension) == 0)
    {
        return "text/css";
    }

    // .html
    else if (strcasecmp("html", extension) == 0)
    {
        return "text/html";
    }

    // .gif
    else if (strcasecmp("gif", extension) == 0)
    {
        return "image/gif";
    }

    // .ico
    else if (strcasecmp("ico", extension) == 0)
    {
        return "image/x-icon";
    }

    // .jpg
    else if (strcasecmp("jpg", extension) == 0)
    {
        return "image/jpeg";
    }

    // .jpg
    else if (strcasecmp("js", extension) == 0)
    {
        return "text/javascript";
    }

    // .png
    else if (strcasecmp("png", extension) == 0)
    {
        return "img/png";
    }

    // unsupported
    else
    {
        return NULL;
    }
}

/**
 * Parses an HTTP request, returning Request-Line plus any headers plus one CRLF.
 */
const char* parse(void)
{
    // ensure client's socket is open
    if (cfd == -1)
    {
        return NULL;
    }

    // number of bytes that will be allowed in an HTTP request
    static char request[LimitRequestLine + LimitRequestFields * LimitRequestFieldSize + LimitRequestBody + 1];

    // read request's headers
    ssize_t length = 0;
    while (true)
    {
        // read from socket
        ssize_t bytes = read(cfd, request + length, sizeof(request) - 1 - length);
        if (bytes == -1)
        {
            respond(500);
            return NULL;
        }

        // if bytes have been read, remember new length
        if (bytes > 0)
        {
            length += bytes;
            request[length] = '\0';
        }

        // else if nothing's been read, socket's been closed
        else
        {
            break;
        }

        // search for CRLF CRLF
        int offset = (length - bytes < 3) ? length - bytes : 3;
        char* haystack = request + length - bytes - offset;
        char* needle = strstr(haystack, "\r\n\r\n");
        if (needle != NULL)
        {
            // trim one CRLF
            *(needle + 2) = '\0';
            break;
        }

        // if buffer's full and we still haven't found CRLF CRLF,
        // then request is too large
        if (length == sizeof(request) - 1)
        {
            respond(413);
            return NULL;
        }
    }

    // success
    return request;
}

/**
 * Resets server's state, deallocating any resources.
 */
void reset(void)
{
    // close file
    if (file != NULL)
    {
        fclose(file);
        file = NULL;
    }

    // close client's socket
    if (cfd != -1)
    {
        close(cfd);
        cfd = -1;
    }
}

/**
 * Responds to client.
 */
bool error(unsigned short code, ...)
//bool respond(unsigned short code, unsigned long long length, const char* type, OCTET* content)
{
    // ensure client's socket is open
    if (cfd == -1)
    {
        return false;
    }

    // determine Status-Line's phrase
    const char* phrase = NULL;
    switch (code)
    {
        case 200: phrase = "OK"; break;
        case 403: phrase = "Forbidden"; break;
        case 404: phrase = "Not Found"; break;
        case 405: phrase = "Method Not Allowed"; break;
        case 413: phrase = "Request Entity Too Large"; break;
        case 414: phrase = "Request-URI Too Long"; break;
        case 418: phrase = "I'm a teapot"; break;
        case 500: phrase = "Internal Server Error"; break;
        case 501: phrase = "Not Implemented"; break;
        case 505: phrase = "HTTP Version Not Supported"; break;
    }
    if (phrase == NULL)
    {
        return false;
    }

    // template for 
    char* template = "<html><head><title>%i %s</title></head><body><h1>%i %s</h1></body></html>";
    char buffer[strlen(template) + 2 * ((int) log10(code) + 1 - 2) + 2 * (strlen(phrase) - 2) + 1];

    // variable arguments
    unsigned long long length;
    char* type;
    OCTET* content;
    if (code == 200)
    {
        va_list ap;
        va_start(ap, code);
        length = va_arg(ap, unsigned long long);
        type = va_arg(ap, char*);
        content = va_arg(ap, OCTET*);
        va_end(ap);
    }
    else
    {
        length = sprintf(buffer, template, code, phrase, code, phrase);
        printf("%s\n", buffer);
        type = "text/html";
        content = buffer;
    }

    // respond with Status-Line
    if (dprintf(cfd, "HTTP/1.1 %i %s\r\n", code, phrase) < 0)
    {
        return false;
    }

    // respond with Connection header
    if (dprintf(cfd, "Connection: close\r\n") < 0)
    {
        return false;
    }
    // respond with Content-Length header
    if (dprintf(cfd, "Content-Length: %lld\r\n", length) < 0)
    {
        return false;
    }

    // respond with Content-Type header
    if (dprintf(cfd, "Content-Type: %s\r\n", type) < 0)
    {
        return false;
    }

    // respond with CRLF
    if (dprintf(cfd, "\r\n") < 0)
    {
        return false;
    }

    // respond with message-body
    if (length > 0)
    {
        if (write(cfd, content, length) == -1)
        {
            return false;
        }
    }

    // responded
    return true;
}

/**
 *
 */
void start(unsigned short port, const char* path)
{
    // path to server's root
    root = realpath(path, NULL);
    if (root == NULL)
    {
        stop();
    }

    // ensure root exists
    if (access(root, F_OK) == -1)
    {
        stop();
    }

    // ensure root is executable
    if (access(root, X_OK) == -1)
    {
        stop();
    }

    // announce root
    printf("Using %s for server's root\n", root);

    // create a socket
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == -1)
    {
        stop();
    }

    // allow reuse of address (to avoid "Address already in use")
    int optval = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // assign name to socket
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1)
    {
        stop();
    }

    // listen for connections
    if (listen(sfd, SOMAXCONN) == -1)
    {
        stop();
    }

    // announce port in use
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(sfd, (struct sockaddr*) &addr, &addrlen) == -1)
    {
        stop();
    }
    printf("Listening on port %i\n", ntohs(addr.sin_port));
}

/**
 * Stop server, deallocating any resources.
 */
void stop(void)
{
    // preserve errno across this function's library calls
    int errsv = errno;

    // reset server's state
    reset();

    // free root, which was allocated by realpath
    if (root != NULL)
    {
        free(root);
    }

    // close server socket
    if (sfd != -1)
    {
        close(sfd);
    }

    // terminate process
    if (errsv == 0)
    {
        // success
        exit(0);
    }
    else
    {
        // failure
        printf("%s\n", strerror(errsv));
        exit(1);
    }
}
