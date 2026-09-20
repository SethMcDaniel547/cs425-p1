#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef TEST
#define main main_exclude
#endif

// Print usage message matching specifications
static void print_usage(const char *progname)
{
    fprintf(stderr, "Usage: %s -f <from> -t <to> [-s subject] [-b body] [-p port]\n", progname);
    fprintf(stderr, "          [-H helo-host] <server>\n\n");
    fprintf(stderr, "  -f <from>       envelope sender, for example you@example.com\n");
    fprintf(stderr, "  -t <to>         envelope recipient\n");
    fprintf(stderr, "  -s <subject>    subject line (default: empty)\n");
    fprintf(stderr, "  -b <body>       message body (default: read from stdin)\n");
    fprintf(stderr, "  -p <port>       port or service name (default: 25)\n");
    fprintf(stderr, "  -H <helo-host>  host name sent with HELO (default: localhost)\n");
    fprintf(stderr, "  <server>        host name or address of the mail server\n");
}

// Read entire message body from stdin if not provided via -b
static char *read_stdin_body(void)
{
    size_t cap = 1024;
    size_t len = 0;
    char *body = malloc(cap);
    if (body == NULL) return NULL;

    int c;
    while ((c = getchar()) != EOF) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *new_body = realloc(body, cap);
            if (new_body == NULL) {
                free(body);
                return NULL;
            }
            body = new_body;
        }
        body[len++] = (char)c;
    }
    body[len] = '\0';
    return body;
}

int main(int argc, char *argv[])
{
    // Print usage and exit 0 when run with no arguments at all
    if (argc == 1) {
        print_usage(argv[0]);
        return 0;
    }

    char *from = NULL;
    char *to = NULL;
    char *subject = NULL;
    char *body = NULL;
    char *port = "25";
    char *helo_host = "localhost";
    int free_body = 0;

    int opt;
    while ((opt = getopt(argc, argv, "f:t:s:b:p:H:")) != -1) {
        switch (opt) {
            case 'f':
                from = optarg;
                break;
            case 't':
                to = optarg;
                break;
            case 's':
                subject = optarg;
                break;
            case 'b':
                body = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'H':
                helo_host = optarg;
                break;
            default:
                print_usage(argv[0]);
                return 1; // Command line is wrong -> Exit 1
        }
    }

    // Check required arguments and remaining positional <server> argument
    if (optind >= argc || from == NULL || to == NULL) {
        print_usage(argv[0]);
        return 1; // Command line is wrong -> Exit 1
    }

    const char *server = argv[optind];

    // If body is not provided via -b, read from stdin
    if (body == NULL) {
        body = read_stdin_body();
        free_body = 1;
    }

    // Connect to socket transport
    int sockfd = socket_connect(server, port);
    if (sockfd < 0) {
        fprintf(stderr, "Error: Failed to connect to server %s on port %s\n", server, port);
        if (free_body) free(body);
        return 2; // Connection or SMTP session failure -> Exit 2
    }

    socket_transport_t transport = { .sockfd = sockfd };
    smtp_config_t config = {
        .helo_host = helo_host,
        .from = from,
        .to = to,
        .subject = subject,
        .body = body
    };

    // Run SMTP session
    int session_res = run_smtp_session(&transport, socket_read_cb, socket_write_cb, &config);

    socket_close(sockfd);
    if (free_body) {
        free(body);
    }

    if (session_res < 0) {
        fprintf(stderr, "Error: SMTP session failed\n");
        return 2; // Connection or SMTP session failure -> Exit 2
    }

    return 0; // Success -> Exit 0
}