// feature test macro requirements
#define _XOPEN_SOURCE 700
#define _XOPEN_SOURCE_EXTENDED

// http://httpd.apache.org/docs/2.2/mod/core.html
#define LimitRequestFields 50
#define LimitRequestFieldSize 4094
#define LimitRequestLine 8190

// number of octets to read when buffering
#define OCTETS 512

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
bool err(unsigned short code);
void handler(int signal);
size_t load(void);
const char* lookup(const char* extension);
bool parse(void);
void reset(void);
void start(unsigned short port, const char* path);
void stop(void);

// server's root
char* root = NULL;

// file descriptor for sockets
int cfd = -1, sfd = -1;

// buffer for request
OCTET* request = NULL;

// FILE pointer for files
FILE* file = NULL;

// buffer for response-body
OCTET* body = NULL;

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
            // parse client's HTTP request
            if (!parse())
            {
                continue;
            }

            // extract request's Request-Line
            const char* haystack = request;
            char* needle = strstr(haystack, "\r\n");
            if (needle == NULL)
            {
                err(400);
                continue;
            }
            else if (needle - haystack + 2 > LimitRequestLine)
            {
                err(414);
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
                err(400);
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
                err(400);
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
                err(414);
                continue;
            }

            // extract Version
            char version[needle - haystack + 1];
            strncpy(version, haystack, needle - haystack);
            version[needle - haystack] = '\0';

            // ensure request's method is GET
            if (strcmp("GET", method) != 0)
            {
                err(405);
                continue;
            }

            // ensure Request-URI starts with abs_path
            if (uri[0] != '/')
            {
                err(501);
                continue;
            }

            // ensure Request-URI is safe
            if (strchr(uri, '"') != NULL)
            {
                err(400);
                continue;
            }

            // ensure request's version is HTTP/1.1
            if (strcmp("HTTP/1.1", version) != 0)
            {
                err(505);
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
                err(404);
                continue;
            }

            // ensure file is readable
            if (access(path, R_OK) == -1)
            {
                err(403);
                continue;
            }

            // extract file's extension
            haystack = path;
            needle = strrchr(haystack, '.');
            if (needle == NULL)
            {
                err(501);
                continue;
            }
            char extension[strlen(needle)];
            strcpy(extension, needle + 1);

            // dynamic content
            if (strcasecmp("php", extension) == 0)
            {
                // open pipe to PHP interpreter
                char* format = "php-cgi -f %s \"%s\"";
                char command[strlen(format) + (strlen(path) - 2) + (strlen(query) - 2) + 1];
                sprintf(command, format, path, query);
                file = popen(command, "r");
                if (file == NULL)
                {
                    err(500);
                    continue;
                }

                // load file
                size_t length = load();
                if (length == -1)
                {
                    err(500);
                    continue;
                }

                // close pipe
                if (pclose(file) == -1)
                {
                    err(500);
                    continue;
                }
    
                // respond to client
                if (dprintf(cfd, "HTTP/1.1 200 OK\r\n") < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Connection: close\r\n") < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Content-Length: %i\r\n", length) < 0)
                {
                    continue;
                }
                if (write(cfd, body, length) == -1)
                {
                    continue;
                }
            }

            // static content
            else
            {
                // look up file's MIME type
                const char* type = lookup(extension);
                if (type == NULL)
                {
                    err(501);
                    continue;
                }

                // open file
                file = fopen(path, "r");
                if (file == NULL)
                {
                    err(500);
                    continue;
                }

                // load file
                size_t length = load();
                if (length == -1)
                {
                    err(500);
                    continue;
                }

                // close file
                if (fclose(file) == -1)
                {
                    err(500);
                    continue;
                }

                // respond to client
                if (dprintf(cfd, "HTTP/1.1 200 OK\r\n") < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Connection: close\r\n") < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Content-Length: %i\r\n", length) < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "Content-Type: %s\r\n", type) < 0)
                {
                    continue;
                }
                if (dprintf(cfd, "\r\n") < 0)
                {
                    continue;
                }
                if (write(cfd, body, length) == -1)
                {
                    continue;
                }
            }
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
 * Handles 4xx and 5xx.
 */
bool err(unsigned short code)
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
        case 400: phrase = "Bad Request"; break;
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

    // template
    char* template = "<html><head><title>%i %s</title></head><body><h1>%i %s</h1></body></html>";
    char content[strlen(template) + 2 * ((int) log10(code) + 1 - 2) + 2 * (strlen(phrase) - 2) + 1];
    int length = sprintf(content, template, code, phrase, code, phrase);

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
    if (dprintf(cfd, "Content-Length: %i\r\n", length) < 0)
    {
        return false;
    }

    // respond with Content-Type header
    if (dprintf(cfd, "Content-Type: text/html\r\n") < 0)
    {
        return false;
    }

    // respond with CRLF
    if (dprintf(cfd, "\r\n") < 0)
    {
        return false;
    }

    // respond with message-body
    if (write(cfd, content, length) == -1)
    {
        return false;
    }

    // log Response-Line
    printf("\033[31m");
    printf("%i %s\n", code, phrase);
    printf("\033[39m");

    return true;
}

/**
 * Loads file into message-body.
 */
size_t load(void)
{
    // ensure file is open
    if (file == NULL)
    {
        return -1;
    }

    // ensure body isn't already loaded
    if (body != NULL)
    {
        return -1;
    }

    // growable buffer for octets
    OCTET buffer[OCTETS];

    // read file
    size_t length = 0;
    while (true)
    {
        // try to read a buffer's worth of octets
        size_t octets = fread(buffer, sizeof(OCTET), sizeof(buffer), file);

        // check for error
        if (ferror(file) != 0)
        {
            if (body != NULL)
            {
                free(body);
                body = NULL;
            }
            return -1;
        }

        // if octets were read, append to body
        if (octets > 0)
        {
            body = realloc(body, length + octets);
            memcpy(body, buffer, length);
            length += octets;
        }

        // check for EOF
        if (feof(file) != 0)
        {
            break;
        }
    }
    return length;
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
 * Parses an HTTP request.
 */
bool parse(void)
{
    // ensure client's socket is open
    if (cfd == -1)
    {
        return false;
    }

    // ensure request isn't already parsed
    if (request != NULL)
    {
        return false;
    }

    // growable buffer for octets
    OCTET buffer[OCTETS];

    // parse request
    ssize_t length = 1;
    request = calloc(sizeof(OCTET), length);
    while (true)
    {
        // read from socket
        ssize_t octets = read(cfd, buffer, sizeof(OCTET) * OCTETS);
        if (octets == -1)
        {
            err(500);
            return false;
        }

        // if octets have been read, remember new length
        if (octets > 0)
        {
            request = realloc(request, length + octets);
            (request + length, buffer, octets);
            length += octets;
            request[length - 1] = '\0';
        }

        // else if nothing's been read, socket's been closed
        else
        {
            break;
        }

        // search for CRLF CRLF
        int offset = (length - octets < 3) ? length - octets : 3;
        char* haystack = request + length - octets - offset;
        char* needle = strstr(haystack, "\r\n\r\n");
        if (needle != NULL)
        {
            // trim one CRLF
            *(needle + 2) = '\0';
            break;
        }

        // if buffer's full and we still haven't found CRLF CRLF,
        // then request is too large
        if (length - 1 >= LimitRequestLine + LimitRequestFields * LimitRequestFieldSize)
        {
            err(413);
            return false;
        }
    }
    return true;
}

/**
 * Resets server's state, deallocating any resources.
 */
void reset(void)
{
    // free request
    if (request != NULL)
    {
        free(request);
        request = NULL;
    }

    // free response's body
    if (body != NULL)
    {
        free(body);
        body = NULL;
    }

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
    printf("\033[33m");
    printf("Using %s for server's root\n", root);
    printf("\033[39m");

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
    printf("\033[33m");
    printf("Listening on port %i\n", ntohs(addr.sin_port));
    printf("\033[39m");
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
        printf("\033[33m");
        printf("%s\n", strerror(errsv));
        printf("\033[39m");
        exit(1);
    }
}
