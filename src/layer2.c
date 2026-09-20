#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Layer 2 Implementation ---

void smtp_reader_init(smtp_reader_t *reader, void *ctx, smtp_read_fn read_cb)
{
    reader->ctx = ctx;
    reader->read_cb = read_cb;
    reader->head = 0;
    reader->tail = 0;
}

static int smtp_reader_getc(smtp_reader_t *reader)
{
    if (reader->head >= reader->tail) {
        long n = reader->read_cb(reader->ctx, reader->buffer, SMTP_BUFFER_SIZE);
        if (n <= 0) return -1;
        reader->head = 0;
        reader->tail = (size_t)n;
    }
    return (unsigned char)reader->buffer[reader->head++];
}

char *smtp_read_line(smtp_reader_t *reader)
{
    size_t cap = 128, len = 0;
    char *line = malloc(cap);
    if (!line) return NULL;

    int c;
    while ((c = smtp_reader_getc(reader)) != -1) {
        if (len + 1 >= cap) {
            cap *= 2;
            char *new_line = realloc(line, cap);
            if (!new_line) { free(line); return NULL; }
            line = new_line;
        }
        line[len++] = (char)c;
        if (c == '\n') break;
    }

    if (len == 0) { free(line); return NULL; }
    line[len] = '\0';
    return line;
}

int smtp_read_reply(smtp_reader_t *reader, int *out_code)
{
    int first_code = -1;
    while (1) {
        char *line = smtp_read_line(reader);
        if (!line) return -1;

        int code = parse_status_code(line);
        if (code == -1) { free(line); return -1; }

        if (first_code == -1) first_code = code;
        else if (code != first_code) { free(line); return -1; }

        int cont = is_continuation_line(line);
        free(line);

        if (!cont) { *out_code = first_code; return 0; }
    }
}

int smtp_send_data(void *ctx, smtp_write_fn write_cb, const char *data)
{
    if (!data) return -1;
    size_t len = strlen(data), total_sent = 0;
    while (total_sent < len) {
        long n = write_cb(ctx, data + total_sent, len - total_sent);
        if (n <= 0) return -1;
        total_sent += (size_t)n;
    }
    return 0;
}

static int send_and_check(void *ctx, smtp_write_fn write_cb, const char *cmd, smtp_reader_t *reader, int exp_code)
{
    if (smtp_send_data(ctx, write_cb, cmd) < 0) return -1;
    int code = 0;
    return (smtp_read_reply(reader, &code) < 0 || code != exp_code) ? -1 : 0;
}

int run_smtp_session(void *ctx, smtp_read_fn read_cb, smtp_write_fn write_cb, const smtp_config_t *config)
{
    if (!config || !config->helo_host || !config->from || !config->to) return -1;
    if (!validate_no_injection(config->helo_host) || !validate_no_injection(config->from) ||
        !validate_no_injection(config->to) || (config->subject && !validate_no_injection(config->subject)))
        return -1;

    smtp_reader_t reader;
    smtp_reader_init(&reader, ctx, read_cb);

    int code = 0;
    if (smtp_read_reply(&reader, &code) < 0 || code != 220) return -1;

    // Helper macro to streamline command execution and memory cleanup
    #define EXEC_CMD(builder_call, exp) \
        do { \
            char *cmd = builder_call; \
            if (!cmd) return -1; \
            int res = send_and_check(ctx, write_cb, cmd, &reader, exp); \
            free(cmd); \
            if (res < 0) return -1; \
        } while(0)

    EXEC_CMD(build_helo_command(config->helo_host), 250);
    EXEC_CMD(build_mail_from_command(config->from), 250);
    EXEC_CMD(build_rcpt_to_command(config->to), 250);
    EXEC_CMD(build_data_command(), 354);

    #undef EXEC_CMD

    // Send Headers
    char header_buf[512];
    if (config->subject) {
        int slen = snprintf(header_buf, sizeof(header_buf), "Subject: %s\r\n", config->subject);
        if (slen > 0 && smtp_send_data(ctx, write_cb, header_buf) < 0) return -1;
    }
    int flen = snprintf(header_buf, sizeof(header_buf), "From: %s\r\n", config->from);
    if (flen > 0 && smtp_send_data(ctx, write_cb, header_buf) < 0) return -1;
    int tlen = snprintf(header_buf, sizeof(header_buf), "To: %s\r\n", config->to);
    if (tlen > 0 && smtp_send_data(ctx, write_cb, header_buf) < 0) return -1;
    if (smtp_send_data(ctx, write_cb, "\r\n") < 0) return -1;

    // Send Body with dot-stuffing
    if (config->body) {
        const char *p = config->body;
        while (*p != '\0') {
            const char *line_end = strchr(p, '\n');
            size_t line_len = line_end ? (size_t)(line_end - p) + 1 : strlen(p);

            char *temp_line = malloc(line_len + 1);
            if (!temp_line) return -1;
            memcpy(temp_line, p, line_len);
            temp_line[line_len] = '\0';

            char *stuffed = dot_stuff_line(temp_line);
            free(temp_line);
            if (!stuffed) return -1;

            int s_res = smtp_send_data(ctx, write_cb, stuffed);
            free(stuffed);
            if (s_res < 0) return -1;

            p += line_len;
        }
    }

    if (smtp_send_data(ctx, write_cb, ".\r\n") < 0) return -1;
    code = 0;
    if (smtp_read_reply(&reader, &code) < 0 || code != 250) return -1;

    char *quit_cmd = build_quit_command();
    if (!quit_cmd) return -1;
    int q_res = send_and_check(ctx, write_cb, quit_cmd, &reader, 221);
    free(quit_cmd);
    return q_res;
}