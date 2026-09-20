#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// --- Layer 1 Implementation ---

int parse_status_code(const char *line)
{
    if (line == NULL || strlen(line) < 3) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        if (!isdigit((unsigned char)line[i])) {
            return -1;
        }
    }
    char code_str[4] = {line[0], line[1], line[2], '\0'};
    return atoi(code_str);
}

int is_continuation_line(const char *line)
{
    if (line == NULL || strlen(line) < 4) {
        return 0; // Not a valid continuation line
    }
    // A continuation line has a hyphen right after the 3-digit code
    if (line[3] == '-') {
        return 1;
    }
    return 0;
}

int validate_no_injection(const char *str)
{
    if (str == NULL) {
        return 1;
    }
    for (size_t i = 0; str[i] != '\0'; i++) {
        // Check for bare \r or \n not part of a valid \r\n pair, 
        // or generally any raw newline/carriage return characters in headers/addresses.
        if (str[i] == '\r' || str[i] == '\n') {
            return 0;
        }
    }
    return 1;
}

char *build_helo_command(const char *helo_host)
{
    if (helo_host == NULL || !validate_no_injection(helo_host)) {
        return NULL;
    }
    int len = snprintf(NULL, 0, "HELO %s\r\n", helo_host);
    if (len < 0) return NULL;
    
    char *cmd = malloc((size_t)len + 1);
    if (cmd == NULL) return NULL;
    
    snprintf(cmd, (size_t)len + 1, "HELO %s\r\n", helo_host);
    return cmd;
}

char *build_mail_from_command(const char *from)
{
    if (from == NULL || !validate_no_injection(from)) {
        return NULL;
    }
    int len = snprintf(NULL, 0, "MAIL FROM:<%s>\r\n", from);
    if (len < 0) return NULL;
    
    char *cmd = malloc((size_t)len + 1);
    if (cmd == NULL) return NULL;
    
    snprintf(cmd, (size_t)len + 1, "MAIL FROM:<%s>\r\n", from);
    return cmd;
}

char *build_rcpt_to_command(const char *to)
{
    if (to == NULL || !validate_no_injection(to)) {
        return NULL;
    }
    int len = snprintf(NULL, 0, "RCPT TO:<%s>\r\n", to);
    if (len < 0) return NULL;
    
    char *cmd = malloc((size_t)len + 1);
    if (cmd == NULL) return NULL;
    
    snprintf(cmd, (size_t)len + 1, "RCPT TO:<%s>\r\n", to);
    return cmd;
}

char *build_data_command(void)
{
    char *cmd = malloc(7); // "DATA\r\n" + null terminator
    if (cmd == NULL) return NULL;
    strcpy(cmd, "DATA\r\n");
    return cmd;
}

char *build_quit_command(void)
{
    char *cmd = malloc(7); // "QUIT\r\n" + null terminator
    if (cmd == NULL) return NULL;
    strcpy(cmd, "QUIT\r\n");
    return cmd;
}

char *dot_stuff_line(const char *line)
{
    if (line == NULL) {
        char *empty = malloc(3);
        if (empty == NULL) return NULL;
        strcpy(empty, "\r\n");
        return empty;
    }

    int starts_with_dot = (line[0] == '.');
    size_t orig_len = strlen(line);
    while (orig_len > 0 && (line[orig_len - 1] == '\r' || line[orig_len - 1] == '\n')) {
        orig_len--;
    }

    size_t extra = starts_with_dot ? 1 : 0;
    size_t alloc_size = extra + orig_len + 3;

    char *stuffed = malloc(alloc_size);
    /* GCOVR_EXCL_START */
    if (stuffed == NULL) {
        return NULL;
    }
    /* GCOVR_EXCL_STOP */

    char *ptr = stuffed;
    if (starts_with_dot) {
        *ptr++ = '.';
    }
    memcpy(ptr, line, orig_len);
    ptr += orig_len;
    *ptr++ = '\r';
    *ptr++ = '\n';
    *ptr = '\0';

    return stuffed;
}