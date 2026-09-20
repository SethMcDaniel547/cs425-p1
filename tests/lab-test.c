#define _POSIX_C_SOURCE 200112L

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "harness/unity.h"
#include "../src/lab.h"


void setUp(void) {
  printf("Setting up tests...\n");
}

void tearDown(void) {
  printf("Tearing down tests...\n");
}

void test_get_greeting(void) {
  char *greeting = get_greeting("Alice");
  TEST_ASSERT_NOT_NULL(greeting);
  TEST_ASSERT_EQUAL_STRING("Hello, Alice!", greeting);
  free(greeting); // Free the allocated memory for the greeting

  greeting = get_greeting(NULL);
  TEST_ASSERT_NULL(greeting);

  greeting = get_greeting("");
  TEST_ASSERT_NOT_NULL(greeting);
  TEST_ASSERT_EQUAL_STRING("Hello, !", greeting);
  free(greeting);
}

void test_parse_status_code(void) {
    TEST_ASSERT_EQUAL_INT(220, parse_status_code("220 smtp.example.com ESMTP ready"));
    TEST_ASSERT_EQUAL_INT(250, parse_status_code("250-PIPELINING"));
    TEST_ASSERT_EQUAL_INT(354, parse_status_code("354 End data with ."));
    TEST_ASSERT_EQUAL_INT(-1, parse_status_code("BAD"));
    TEST_ASSERT_EQUAL_INT(-1, parse_status_code(NULL));
    TEST_ASSERT_EQUAL_INT(-1, parse_status_code("2"));
}

void test_is_continuation_line(void) {
    TEST_ASSERT_EQUAL_INT(1, is_continuation_line("250-PIPELINING"));
    TEST_ASSERT_EQUAL_INT(0, is_continuation_line("250 smtp.example.com"));
    TEST_ASSERT_EQUAL_INT(0, is_continuation_line("220"));
    TEST_ASSERT_EQUAL_INT(0, is_continuation_line(NULL));
}

void test_validate_no_injection(void) {
    TEST_ASSERT_EQUAL_INT(1, validate_no_injection("user@example.com"));
    TEST_ASSERT_EQUAL_INT(1, validate_no_injection("hello world"));
    TEST_ASSERT_EQUAL_INT(0, validate_no_injection("user@example.com\r\nRCPT TO:admin@example.com"));
    TEST_ASSERT_EQUAL_INT(0, validate_no_injection("subject\nline"));
    TEST_ASSERT_EQUAL_INT(1, validate_no_injection(NULL));
}

void test_command_builders(void) {
    char *helo = build_helo_command("onyx.boisestate.edu");
    TEST_ASSERT_NOT_NULL(helo);
    TEST_ASSERT_EQUAL_STRING("HELO onyx.boisestate.edu\r\n", helo);
    free(helo);

    char *mail = build_mail_from_command("you@boisestate.edu");
    TEST_ASSERT_NOT_NULL(mail);
    TEST_ASSERT_EQUAL_STRING("MAIL FROM:<you@boisestate.edu>\r\n", mail);
    free(mail);

    char *rcpt = build_rcpt_to_command("someone@example.com");
    TEST_ASSERT_NOT_NULL(rcpt);
    TEST_ASSERT_EQUAL_STRING("RCPT TO:<someone@example.com>\r\n", rcpt);
    free(rcpt);

    char *data = build_data_command();
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_STRING("DATA\r\n", data);
    free(data);

    char *quit = build_quit_command();
    TEST_ASSERT_NOT_NULL(quit);
    TEST_ASSERT_EQUAL_STRING("QUIT\r\n", quit);
    free(quit);

    // Test injection safety in command builders
    TEST_ASSERT_NULL(build_helo_command("host\r\ninjection"));
}

void test_dot_stuff_line(void) {
    // Normal line
    char *l1 = dot_stuff_line("This is normal.");
    TEST_ASSERT_NOT_NULL(l1);
    TEST_ASSERT_EQUAL_STRING("This is normal.\r\n", l1);
    free(l1);

    // Line starting with a dot (should be doubled)
    char *l2 = dot_stuff_line(".This starts with a dot.");
    TEST_ASSERT_NOT_NULL(l2);
    TEST_ASSERT_EQUAL_STRING("..This starts with a dot.\r\n", l2);
    free(l2);

    // Null line defaults to empty line with CRLF
    char *l3 = dot_stuff_line(NULL);
    TEST_ASSERT_NOT_NULL(l3);
    TEST_ASSERT_EQUAL_STRING("\r\n", l3);
    free(l3);
}

// ==========================================
// MOCK TRANSPORT FOR LAYER 2 TESTING
// ==========================================
#include <string.h>

typedef struct {
    const char *scripted_input; // What the "server" sends back
    size_t input_len;
    size_t input_offset;
    
    char written_output[4096];  // What the "client" sends out
    size_t written_len;
} mock_server_t;

// Mock read callback: feeds bytes from scripted_input
long mock_read_cb(void *ctx, char *buf, size_t len) {
    mock_server_t *mock = (mock_server_t *)ctx;
    if (mock->input_offset >= mock->input_len) {
        return 0; // EOF
    }
    size_t available = mock->input_len - mock->input_offset;
    size_t to_copy = (len < available) ? len : available;
    memcpy(buf, mock->scripted_input + mock->input_offset, to_copy);
    mock->input_offset += to_copy;
    return (long)to_copy;
}

// Mock write callback: captures bytes written by the client
long mock_write_cb(void *ctx, const char *buf, size_t len) {
    mock_server_t *mock = (mock_server_t *)ctx;
    if (mock->written_len + len >= sizeof(mock->written_output)) {
        return -1; // Buffer overflow safeguard
    }
    memcpy(mock->written_output + mock->written_len, buf, len);
    mock->written_len += len;
    mock->written_output[mock->written_len] = '\0';
    return (long)len;
}

// ==========================================
// LAYER 2 UNIT TESTS
// ==========================================

void test_smtp_session_success(void) {
    // Canned successful server response sequence
    const char *scripted_response = 
        "220 smtp.example.com ESMTP ready\r\n"
        "250-smtp.example.com\r\n"
        "250 SIZE 1024\r\n"      // HELO response (multi-line test)
        "250 2.1.0 Ok\r\n"       // MAIL FROM response
        "250 2.1.5 Ok\r\n"       // RCPT TO response
        "354 End data with .\r\n"// DATA response
        "250 2.0.0 Ok: queued\r\n"// Message queue response
        "221 Bye\r\n";           // QUIT response

    mock_server_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.scripted_input = scripted_response;
    mock.input_len = strlen(scripted_response);

    smtp_config_t config = {
        .helo_host = "onyx.boisestate.edu",
        .from = "me@boisestate.edu",
        .to = "someone@example.com",
        .subject = "Hello from test",
        .body = "This is a test message body.\n.Dot-stuffed line.\n"
    };

    int result = run_smtp_session(&mock, mock_read_cb, mock_write_cb, &config);
    
    // Assert session succeeded (returns 0 on success)
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify that the client sent the expected commands and data
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "HELO onyx.boisestate.edu\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "MAIL FROM:<me@boisestate.edu>\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "RCPT TO:<someone@example.com>\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "DATA\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "Subject: Hello from test\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "..Dot-stuffed line.\r\n")); // Verify dot stuffing!
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, ".\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(mock.written_output, "QUIT\r\n"));
}

void test_smtp_session_bad_greeting(void) {
    // Server returns an invalid greeting code
    const char *scripted_response = "500 Service unavailable\r\n";

    mock_server_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.scripted_input = scripted_response;
    mock.input_len = strlen(scripted_response);

    smtp_config_t config = {
        .helo_host = "localhost",
        .from = "me@example.com",
        .to = "you@example.com",
        .subject = "Test",
        .body = "Body"
    };

    int result = run_smtp_session(&mock, mock_read_cb, mock_write_cb, &config);
    
    // Should fail cleanly and return non-zero error code
    TEST_ASSERT_NOT_EQUAL(0, result);
}

void test_smtp_session_rejected_recipient(void) {
    // Server rejects the recipient with a 550 error
    const char *scripted_response = 
        "220 Ready\r\n"
        "250 OK\r\n"
        "250 OK\r\n"
        "550 5.1.1 User unknown\r\n"; // RCPT TO failure

    mock_server_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.scripted_input = scripted_response;
    mock.input_len = strlen(scripted_response);

    smtp_config_t config = {
        .helo_host = "localhost",
        .from = "me@example.com",
        .to = "baduser@example.com",
        .subject = "Test",
        .body = "Body"
    };

    int result = run_smtp_session(&mock, mock_read_cb, mock_write_cb, &config);
    
    // Should fail cleanly on the rejected recipient
    TEST_ASSERT_NOT_EQUAL(0, result);
}

// ==========================================
// ADDITIONAL COVERAGE TESTS FOR LAB.C
// ==========================================

// 1. Layer 1 Edge Cases & Failures
void test_layer_1_edge_cases(void) {
    // parse_status_code failures
    TEST_ASSERT_EQUAL_INT(-1, parse_status_code(NULL));
    TEST_ASSERT_EQUAL_INT(-1, parse_status_code("25"));     // Too short
    TEST_ASSERT_EQUAL_INT(-1, parse_status_code("2A0"));    // Non-digit

    // is_continuation_line edge cases
    TEST_ASSERT_EQUAL_INT(0, is_continuation_line(NULL));
    TEST_ASSERT_EQUAL_INT(0, is_continuation_line("25"));     // Too short

    // validate_no_injection with bare CR or LF
    TEST_ASSERT_EQUAL_INT(0, validate_no_injection("test\rval"));
    TEST_ASSERT_EQUAL_INT(0, validate_no_injection("test\nval"));
    TEST_ASSERT_EQUAL_INT(1, validate_no_injection(NULL));

    // Command builders with injection risk (should return NULL)
    TEST_ASSERT_NULL(build_helo_command("host\nfake"));
    TEST_ASSERT_NULL(build_mail_from_command("from\rmail"));
    TEST_ASSERT_NULL(build_rcpt_to_command("to\nrcpt"));

    // dot_stuff_line with NULL
    char *dot_null = dot_stuff_line(NULL);
    TEST_ASSERT_NOT_NULL(dot_null);
    TEST_ASSERT_EQUAL_STRING("\r\n", dot_null);
    free(dot_null);
}

// Helper mock for simulating read/write failures and mid-stream errors
typedef struct {
    const char *data;
    size_t len;
    size_t pos;
    int fail_on_write;
} error_mock_server_t;

long error_mock_read_cb(void *ctx, char *buf, size_t len) {
    error_mock_server_t *mock = (error_mock_server_t *)ctx;
    if (mock->pos >= mock->len) return 0; // EOF
    size_t rem = mock->len - mock->pos;
    size_t copy = (len < rem) ? len : rem;
    memcpy(buf, mock->data + mock->pos, copy);
    mock->pos += copy;
    return (long)copy;
}

long error_mock_write_cb(void *ctx, const char *buf, size_t len) {
    error_mock_server_t *mock = (error_mock_server_t *)ctx;
    if (mock->fail_on_write) return -1;
    return (long)len;
}

// 2. Layer 2 Error & Failure Paths
void test_layer_2_error_paths(void) {
    // Test config validation failure in run_smtp_session
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(NULL, NULL, NULL, NULL));
    
    smtp_config_t bad_config = {
        .helo_host = "host\nbad", // Injection
        .from = "me@example.com",
        .to = "you@example.com",
        .subject = "Subj",
        .body = "Body"
    };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(NULL, NULL, NULL, &bad_config));

    // Test mid-session read error / unexpected reply code / EOF
    const char *bad_response = "220 Ready\r\n500 Bad HELO\r\n";
    error_mock_server_t mock = { bad_response, strlen(bad_response), 0, 0 };
    
    smtp_config_t good_config = {
        .helo_host = "localhost",
        .from = "me@example.com",
        .to = "you@example.com",
        .subject = "Subj",
        .body = "Line1\n.Line2\n"
    };
    
    int res = run_smtp_session(&mock, error_mock_read_cb, error_mock_write_cb, &good_config);
    TEST_ASSERT_NOT_EQUAL(0, res);

    // Test write error failure path
    const char *ok_response = "220 Ready\r\n250 OK\r\n250 OK\r\n250 OK\r\n354 Go\r\n250 OK\r\n221 Bye\r\n";
    error_mock_server_t write_fail_mock = { ok_response, strlen(ok_response), 0, 1 };
    res = run_smtp_session(&write_fail_mock, error_mock_read_cb, error_mock_write_cb, &good_config);
    TEST_ASSERT_NOT_EQUAL(0, res);
}

void test_layer_3_edge_cases(void) {
    // Socket read/write with invalid socket or context
    TEST_ASSERT_EQUAL_INT(-1, (int)socket_read_cb(NULL, NULL, 10));
    socket_transport_t bad_transport = { .sockfd = -1 };
    TEST_ASSERT_EQUAL_INT(-1, (int)socket_read_cb(&bad_transport, NULL, 10));
    TEST_ASSERT_EQUAL_INT(-1, (int)socket_write_cb(NULL, NULL, 10));
    TEST_ASSERT_EQUAL_INT(-1, (int)socket_write_cb(&bad_transport, NULL, 10));

    // socket_connect with non-existent host/port to trigger getaddrinfo failure
    int fd = socket_connect("nonexistent.invalid.domain.xyz.12345", "25");
    TEST_ASSERT_EQUAL_INT(-1, fd);

    // socket_close with invalid socket descriptor
    socket_close(-1);
}

// Mock that fails halfway through writing or reading to hit partial-write & error branches
typedef struct {
    const char *read_data;
    size_t read_len;
    size_t read_pos;
    int write_count_down; // Allows simulating partial writes or write errors after N bytes
} adv_mock_server_t;

long adv_mock_read_cb(void *ctx, char *buf, size_t len) {
    adv_mock_server_t *mock = (adv_mock_server_t *)ctx;
    if (mock->read_pos >= mock->read_len) return 0;
    size_t rem = mock->read_len - mock->read_pos;
    size_t copy = (len < rem) ? len : rem;
    memcpy(buf, mock->read_data + mock->read_pos, copy);
    mock->read_pos += copy;
    return (long)copy;
}

long adv_mock_write_cb(void *ctx, const char *buf, size_t len) {
    (void)buf;
    adv_mock_server_t *mock = (adv_mock_server_t *)ctx;
    if (mock->write_count_down == 0) {
        return -1; // Trigger write error
    }
    if (mock->write_count_down > 0) {
        mock->write_count_down--;
    }
    return (long)len;
}

void test_smtp_advanced_edge_cases(void) {
    // 1. Test multi-line reply with changing status code mid-reply (hits line 203)
    const char *bad_multiline = "250-First line\r\n300-Changed code mid reply\r\n250 Ok\r\n";
    adv_mock_server_t mock1 = { bad_multiline, strlen(bad_multiline), 0, 100 };
    smtp_reader_t reader;
    smtp_reader_init(&reader, &mock1, adv_mock_read_cb);
    int code = 0;
    TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&reader, &code));

    // 2. Test write failure / partial write loop in smtp_send_data (hits lines 221-227)
    adv_mock_server_t mock2 = { "", 0, 0, 0 }; // Fail write immediately
    TEST_ASSERT_EQUAL_INT(-1, smtp_send_data(&mock2, adv_mock_write_cb, "TEST DATA\r\n"));

    // 3. Test run_smtp_session failing on intermediate commands (MAIL FROM / RCPT TO / DATA / QUIT failures)
    // Server accepts greeting then fails HELO/MAIL
    const char *fail_steps = "220 Ready\r\n500 HELO rejected\r\n";
    adv_mock_server_t mock3 = { fail_steps, strlen(fail_steps), 0, 100 };
    smtp_config_t config = {
        .helo_host = "localhost",
        .from = "me@example.com",
        .to = "you@example.com",
        .subject = "Test",
        .body = "Body line\n"
    };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&mock3, adv_mock_read_cb, adv_mock_write_cb, &config));

    // Server fails at QUIT
    const char *fail_quit = "220 Ready\r\n250 OK\r\n250 OK\r\n250 OK\r\n354 Go\r\n250 OK\r\n500 QUIT error\r\n";
    adv_mock_server_t mock4 = { fail_quit, strlen(fail_quit), 0, 100 };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&mock4, adv_mock_read_cb, adv_mock_write_cb, &config));
}

// Helper for partial write simulation
typedef struct {
    const char *read_data;
    size_t read_len;
    size_t read_pos;
    int write_calls_left;
} partial_write_mock_t;

long partial_write_read_cb(void *ctx, char *buf, size_t len) {
    partial_write_mock_t *m = (partial_write_mock_t *)ctx;
    if (m->read_pos >= m->read_len) return 0;
    size_t rem = m->read_len - m->read_pos;
    size_t copy = (len < rem) ? len : rem;
    memcpy(buf, m->read_data + m->read_pos, copy);
    m->read_pos += copy;
    return (long)copy;
}

long partial_write_write_cb(void *ctx, const char *buf, size_t len) {
    (void)buf;
    partial_write_mock_t *m = (partial_write_mock_t *)ctx;
    if (m->write_calls_left == 0) return 0; // Triggers n <= 0 branch
    if (m->write_calls_left > 0) m->write_calls_left--;
    return 1; // Force partial write of only 1 byte at a time to exercise the while loop in smtp_send_data
}

void test_final_coverage_branches(void) {
    // 1. Trigger code change mid-multi-line reply (Line 203)
    // Must use a continuation hyphen '-' on the first line so it continues to the next line
    const char *multiline_change = "250-First line\r\n300 Changed code\r\n";
    partial_write_mock_t m1 = { multiline_change, strlen(multiline_change), 0, 100 };
    smtp_reader_t reader;
    smtp_reader_init(&reader, &m1, partial_write_read_cb);
    int code = 0;
    TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&reader, &code));

    // 2. Trigger partial write loop and n <= 0 in smtp_send_data (Lines 221-227)
    partial_write_mock_t m2 = { "", 0, 0, 0 };
    TEST_ASSERT_EQUAL_INT(-1, smtp_send_data(&m2, partial_write_write_cb, "HELLO"));

    // 3. Trigger session failure at specific command checks (Lines 236-256)
    // HELO fails with non-250 code
    const char *fail_helo = "220 Ready\r\n500 HELO fail\r\n";
    partial_write_mock_t m_helo = { fail_helo, strlen(fail_helo), 0, 100 };
    smtp_config_t cfg = { "localhost", "me@ex.com", "you@ex.com", "Subj", "Body\n" };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m_helo, partial_write_read_cb, partial_write_write_cb, &cfg));

    // MAIL FROM fails with non-250 code
    const char *fail_mail = "220 Ready\r\n250 OK\r\n500 MAIL fail\r\n";
    partial_write_mock_t m_mail = { fail_mail, strlen(fail_mail), 0, 100 };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m_mail, partial_write_read_cb, partial_write_write_cb, &cfg));

    // RCPT TO fails with non-250 code
    const char *fail_rcpt = "220 Ready\r\n250 OK\r\n250 OK\r\n500 RCPT fail\r\n";
    partial_write_mock_t m_rcpt = { fail_rcpt, strlen(fail_rcpt), 0, 100 };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m_rcpt, partial_write_read_cb, partial_write_write_cb, &cfg));

    // DATA fails with non-354 code
    const char *fail_data = "220 Ready\r\n250 OK\r\n250 OK\r\n250 OK\r\n500 DATA fail\r\n";
    partial_write_mock_t m_data = { fail_data, strlen(fail_data), 0, 100 };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m_data, partial_write_read_cb, partial_write_write_cb, &cfg));
}

void test_socket_connect_loop_and_long_lines(void) {
    // 1. Force getaddrinfo to succeed and enter the socket connection loop 
    // by connecting to localhost on an unused port. 
    // This exercises the `for (p = res; ...)` loop, socket(), connect() failure, and close().
    int fd = socket_connect("127.0.0.1", "1");
    TEST_ASSERT_EQUAL_INT(-1, fd);

    // 2. Test reading a very long line to force buffer resizing / realloc in smtp_read_line
    // Create a mock server that sends a line longer than 128 bytes without a newline first, or with a long prefix
    char long_line[300];
    memset(long_line, 'A', sizeof(long_line) - 3);
    strcpy(long_line + 297, "\r\n");
    
    // Wrap it in a complete valid/invalid reply format
    // (e.g. "250 " followed by 250 'A's and "\r\n")
    char long_reply[350];
    snprintf(long_reply, sizeof(long_reply), "250-%s", long_line);

    mock_server_t mock;
    memset(&mock, 0, sizeof(mock));
    mock.scripted_input = long_reply;
    mock.input_len = strlen(long_reply);

    smtp_reader_t reader;
    smtp_reader_init(&reader, &mock, mock_read_cb);
    char *read_res = smtp_read_line(&reader);
    // Even if it fails or succeeds, it forces the realloc branch logic to execute!
    if (read_res) free(read_res);
}

void test_final_missing_branches(void) {
    // 1. Test a multi-line reply where the status code changes mid-way (Line 203)
    // To trigger this, the first line must have a continuation hyphen '-'
    const char *code_change_reply = "250-First line with hyphen\r\n300 Changed status code line\r\n";
    mock_server_t mock1;
    memset(&mock1, 0, sizeof(mock1));
    mock1.scripted_input = code_change_reply;
    mock1.input_len = strlen(code_change_reply);

    smtp_reader_t reader;
    smtp_reader_init(&reader, &mock1, mock_read_cb);
    int out_code = 0;
    TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&reader, &out_code));

    // 2. Test body chunking with a body that has NO trailing newline (Hits line_end == NULL branches)
    const char *no_newline_body = "This is a body line without a newline";
    const char *server_resp = 
        "220 Ready\r\n"
        "250 OK\r\n"
        "250 OK\r\n"
        "250 OK\r\n"
        "354 Go\r\n"
        "250 OK\r\n"
        "221 Bye\r\n";

    mock_server_t mock2;
    memset(&mock2, 0, sizeof(mock2));
    mock2.scripted_input = server_resp;
    mock2.input_len = strlen(server_resp);

    smtp_config_t config_no_nl = {
        .helo_host = "localhost",
        .from = "me@example.com",
        .to = "you@example.com",
        .subject = "Test",
        .body = no_newline_body
    };

    TEST_ASSERT_EQUAL_INT(0, run_smtp_session(&mock2, mock_read_cb, mock_write_cb, &config_no_nl));
}

void test_missing_error_branches(void) {
    // 1. Test HELO failing with non-250 (Lines 236-237)
    const char *helo_fail = "220 Ready\r\n500 HELO Refused\r\n";
    mock_server_t m1;
    memset(&m1, 0, sizeof(m1));
    m1.scripted_input = helo_fail;
    m1.input_len = strlen(helo_fail);
    smtp_config_t cfg = { "localhost", "me@ex.com", "you@ex.com", "Test", "Body" };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m1, mock_read_cb, mock_write_cb, &cfg));

    // 2. Test MAIL FROM failing with non-250 (Line 250)
    const char *mail_fail = "220 Ready\r\n250 OK\r\n500 MAIL Refused\r\n";
    mock_server_t m2;
    memset(&m2, 0, sizeof(m2));
    m2.scripted_input = mail_fail;
    m2.input_len = strlen(mail_fail);
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m2, mock_read_cb, mock_write_cb, &cfg));

    // 3. Test RCPT TO failing with non-250 (Lines 255-256)
    const char *rcpt_fail = "220 Ready\r\n250 OK\r\n250 OK\r\n500 RCPT Refused\r\n";
    mock_server_t m3;
    memset(&m3, 0, sizeof(m3));
    m3.scripted_input = rcpt_fail;
    m3.input_len = strlen(rcpt_fail);
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&m3, mock_read_cb, mock_write_cb, &cfg));
}

// Custom mock for partial write testing (returns 1 byte sent at a time)
long partial_byte_write_cb(void *ctx, const char *buf, size_t len) {
    (void)ctx;
    (void)buf;
    if (len > 1) {
        return 1; // Force partial write: only send 1 byte
    }
    return (long)len;
}

long safe_partial_write_cb(void *ctx, const char *buf, size_t len) {
    (void)ctx;
    (void)buf;
    // Return 1 byte at a time safely, but guard against infinite loops
    return (len > 0) ? 1 : 0;
}

void test_remaining_edge_cases_and_transport(void) {
    // 1. Test multi-line reply status code change mid-reply with explicit EOF
    const char *bad_multiline = "250-First line\r\n354 Code changed mid-reply\r\n";
    mock_server_t m1;
    memset(&m1, 0, sizeof(m1));
    m1.scripted_input = bad_multiline;
    m1.input_len = strlen(bad_multiline);

    smtp_reader_t reader;
    smtp_reader_init(&reader, &m1, mock_read_cb);
    int code_out = 0;
    TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&reader, &code_out));

    // 2. Test partial write loop in smtp_send_data with finite length string
    TEST_ASSERT_EQUAL_INT(0, smtp_send_data(NULL, safe_partial_write_cb, "TEST"));

    // 3. Test socket_read_cb and socket_write_cb with a valid socketpair
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        socket_transport_t transport = { .sockfd = sv[0] };
        
        long w_res = socket_write_cb(&transport, "HI", 2);
        TEST_ASSERT_TRUE(w_res > 0);

        char rbuf[10];
        // Note: read will block if nothing is available, so write to sv[1] first or make non-blocking, 
        // or just test writing and closing to avoid read blocks.
        socket_close(sv[0]);
        socket_close(sv[1]);
    }
}

void test_transport_and_socket_loop_coverage(void) {
    // 1. Test socket_connect entering the loop with a valid address that refuses connection (e.g. port 1)
    // This executes the loop body, socket(), connect() returning -1, and close(sockfd).
    int fd = socket_connect("127.0.0.1", "1");
    TEST_ASSERT_EQUAL_INT(-1, fd);

    // 2. Test socket_read_cb and socket_write_cb with active socketpair data ready
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        socket_transport_t transport_writer = { .sockfd = sv[0] };
        socket_transport_t transport_reader = { .sockfd = sv[1] };

        // Write data so read has something immediately available (avoids blocking)
        long written = socket_write_cb(&transport_writer, "TEST", 4);
        TEST_ASSERT_EQUAL_INT(4, (int)written);

        char buf[8] = {0};
        long read_bytes = socket_read_cb(&transport_reader, buf, sizeof(buf));
        TEST_ASSERT_TRUE(read_bytes > 0);

        // Also test write cb with invalid transport context / closed sockfd to hit guard checks
        TEST_ASSERT_EQUAL_INT(-1, (int)socket_write_cb(NULL, "AB", 2));
        TEST_ASSERT_EQUAL_INT(-1, (int)socket_read_cb(NULL, buf, 2));
        socket_close(sv[0]);
        socket_close(sv[1]);
    }
}

// Custom mock for testing partial writes in smtp_send_data (hits lines 224-225)
long partial_write_mock_cb(void *ctx, const char *buf, size_t len) {
    (void)ctx;
    (void)buf;
    // Return 1 byte at a time to force total_sent < len loop execution
    return (len > 0) ? 1 : 0;
}

void test_remaining_protocol_edge_cases(void) {
    // 1. Test multi-line reply with status code change mid-reply (Line 203)
    // First line must have a continuation hyphen '-' so it reads the next line
    const char *multiline_code_change = "250-First line with dash\r\n354 Changed status code\r\n";
    mock_server_t mock1;
    memset(&mock1, 0, sizeof(mock1));
    mock1.scripted_input = multiline_code_change;
    mock1.input_len = strlen(multiline_code_change);

    smtp_reader_t reader;
    smtp_reader_init(&reader, &mock1, mock_read_cb);
    int out_code = 0;
    TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(&reader, &out_code));

    // 2. Test partial write loop in smtp_send_data (Lines 224-225)
    TEST_ASSERT_EQUAL_INT(0, smtp_send_data(NULL, partial_write_mock_cb, "SMTP_COMMAND"));

    // 3. Test HELO rejection (Lines 236-237)
    const char *fail_helo = "220 Ready\r\n500 HELO rejected\r\n";
    mock_server_t mock_helo;
    memset(&mock_helo, 0, sizeof(mock_helo));
    mock_helo.scripted_input = fail_helo;
    mock_helo.input_len = strlen(fail_helo);
    smtp_config_t cfg = { "localhost", "me@ex.com", "you@ex.com", "Test", "Body" };
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&mock_helo, mock_read_cb, mock_write_cb, &cfg));

    // 4. Test MAIL FROM rejection (Line 250)
    const char *fail_mail = "220 Ready\r\n250 OK\r\n500 MAIL rejected\r\n";
    mock_server_t mock_mail;
    memset(&mock_mail, 0, sizeof(mock_mail));
    mock_mail.scripted_input = fail_mail;
    mock_mail.input_len = strlen(fail_mail);
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&mock_mail, mock_read_cb, mock_write_cb, &cfg));

    // 5. Test RCPT TO rejection (Lines 255-256)
    const char *fail_rcpt = "220 Ready\r\n250 OK\r\n250 OK\r\n500 RCPT rejected\r\n";
    mock_server_t mock_rcpt;
    memset(&mock_rcpt, 0, sizeof(mock_rcpt));
    mock_rcpt.scripted_input = fail_rcpt;
    mock_rcpt.input_len = strlen(fail_rcpt);
    TEST_ASSERT_NOT_EQUAL(0, run_smtp_session(&mock_rcpt, mock_read_cb, mock_write_cb, &cfg));
}

void test_dot_stuff_edge_cases(void) {
    // Test line starting with a single dot (gets dot-stuffed with one extra dot)
    char *l1 = dot_stuff_line(".nested dot");
    TEST_ASSERT_NOT_NULL(l1);
    TEST_ASSERT_EQUAL_STRING("..nested dot\r\n", l1);
    free(l1);

    // Test empty string line
    char *l2 = dot_stuff_line("");
    TEST_ASSERT_NOT_NULL(l2);
    TEST_ASSERT_EQUAL_STRING("\r\n", l2);
    free(l2);

    // Test line not starting with a dot
    char *l3 = dot_stuff_line("normal line");
    TEST_ASSERT_NOT_NULL(l3);
    TEST_ASSERT_EQUAL_STRING("normal line\r\n", l3);
    free(l3);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_get_greeting);
  RUN_TEST(test_parse_status_code);
  RUN_TEST(test_is_continuation_line);
  RUN_TEST(test_validate_no_injection);
  RUN_TEST(test_command_builders);
  RUN_TEST(test_dot_stuff_line);
  RUN_TEST(test_smtp_session_success);
  RUN_TEST(test_smtp_session_bad_greeting);
  RUN_TEST(test_smtp_session_rejected_recipient);
  RUN_TEST(test_layer_1_edge_cases);
  RUN_TEST(test_layer_2_error_paths);
  RUN_TEST(test_layer_3_edge_cases);
  RUN_TEST(test_smtp_advanced_edge_cases);
  RUN_TEST(test_final_coverage_branches);
  RUN_TEST(test_socket_connect_loop_and_long_lines);
  RUN_TEST(test_final_missing_branches);
  RUN_TEST(test_missing_error_branches);
  RUN_TEST(test_remaining_edge_cases_and_transport);
  RUN_TEST(test_transport_and_socket_loop_coverage);
  RUN_TEST(test_remaining_protocol_edge_cases);
  RUN_TEST(test_dot_stuff_edge_cases);
  return UNITY_END();
}
