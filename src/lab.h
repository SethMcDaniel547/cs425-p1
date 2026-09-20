#ifndef LAB_H
#define LAB_H

#include <sys/types.h>
#include <stddef.h>

/** * @brief Returns a greeting message.
 *
 * This function returns a string that contains a greeting message.
 * The string is allocated with malloc and should be freed by the caller.
 * @param name The name to include in the greeting.
 * @return A greeting string.
 */
char* get_greeting(const char* restrict name);


// ==========================================
// LAYER 1: Pure Protocol Helpers
// ==========================================

// Parses the 3-digit status code from a reply line. Returns -1 on error.
int parse_status_code(const char *line);

// Returns 1 if the reply line is a continuation (has '-' after code), 0 if final, -1 on error.
int is_continuation_line(const char *line);

// Checks if a string contains bare CR or LF (security check against SMTP injection).
// Returns 1 if safe, 0 if it contains injection risk.
int validate_no_injection(const char *str);

// Command builders (allocates a new string, caller must free).
char *build_helo_command(const char *helo_host);
char *build_mail_from_command(const char *from);
char *build_rcpt_to_command(const char *to);
char *build_data_command(void);
char *build_quit_command(void);

// Dot-stuffs a single body line (duplicates leading dot). 
// Returns a newly allocated string with \r\n appended.
char *dot_stuff_line(const char *line);

// ==========================================
// LAYER 2: Abstract Session & I/O
// ==========================================

// Callback function pointer types for I/O abstraction
typedef long (*smtp_read_fn)(void *ctx, char *buf, size_t len);
typedef long (*smtp_write_fn)(void *ctx, const char *buf, size_t len);

// Buffered reader structure to handle TCP stream fragmentation
#define SMTP_BUFFER_SIZE 4096

typedef struct {
    void *ctx;
    smtp_read_fn read_cb;
    char buffer[SMTP_BUFFER_SIZE];
    size_t head;
    size_t tail;
} smtp_reader_t;

// Initialize the buffered reader
void smtp_reader_init(smtp_reader_t *reader, void *ctx, smtp_read_fn read_cb);

// Read a single line terminated by \r\n (includes \r\n in the returned string, dynamically allocated)
// Returns NULL on error or EOF.
char *smtp_read_line(smtp_reader_t *reader);

// Read an entire multi-line reply (handles continuation '-' vs final ' ')
// Returns the full reply string (or allocated buffer) and populates out_code.
// Returns 0 on success, -1 on failure/unexpected code.
int smtp_read_reply(smtp_reader_t *reader, int *out_code);

// Send a command or raw data via write callback
int smtp_send_data(void *ctx, smtp_write_fn write_cb, const char *data);

// Configuration structure for the SMTP session parameters
typedef struct {
    const char *helo_host;
    const char *from;
    const char *to;
    const char *subject;
    const char *body; // Can be multi-line string or line-by-line provider
} smtp_config_t;

// Run the full SMTP session using read/write callbacks and context
int run_smtp_session(void *ctx, smtp_read_fn read_cb, smtp_write_fn write_cb, const smtp_config_t *config);


// ==========================================
// LAYER 3: Socket Transport & Main Runner
// ==========================================

// Socket transport context structure
typedef struct {
    int sockfd;
} socket_transport_t;

// Socket read/write callback implementations matching smtp_read_fn and smtp_write_fn
long socket_read_cb(void *ctx, char *buf, size_t len);
long socket_write_cb(void *ctx, const char *buf, size_t len);

// Helper to connect to a server and port using getaddrinfo
int socket_connect(const char *host, const char *port_str);
void socket_close(int sockfd);


#endif // LAB_H
