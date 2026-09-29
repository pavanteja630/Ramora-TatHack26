#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

enum ResCode {
    RES_OK = 0,
    RES_ERR = 1,
    RES_NX = 2,
    RES_TY = 3
};

enum Tag {
    TAG_NULL = 0,
    TAG_INT = 1,
    TAG_STR = 2,
    TAG_ARR = 3,
    TAG_INT64 = 4
};

struct ReplyItem {
    uint8_t tag;
    uint32_t len;
    char* str;
    int32_t i32;
    int64_t i64;
};

struct RamoraReply {
    uint8_t code;
    uint32_t count;
    struct ReplyItem items[1024];
};

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_START(name) do { \
    g_tests_run++; \
    printf("  [TEST %02d] %-45s ... ", g_tests_run, name); \
    fflush(stdout); \
} while (0)

#define TEST_PASS() do { \
    g_tests_passed++; \
    printf("\033[1;32mPASS\033[0m\n"); \
} while (0)

#define TEST_FAIL(fmt, ...) do { \
    g_tests_failed++; \
    printf("\033[1;31mFAIL\033[0m (" fmt ")\n", ##__VA_ARGS__); \
} while (0)

static ssize_t write_all(int fd, const void *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, (const char *)buf + off, len - off);
        if (n <= 0) return -1;
        off += n;
    }
    return (ssize_t)off;
}

static ssize_t read_all(int fd, void *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, (char *)buf + off, len - off);
        if (n <= 0) return -1;
        off += n;
    }
    return (ssize_t)off;
}

static int send_cmd(int fd, int argc, ...) {
    uint8_t buf[8192];
    uint8_t *p = buf;

    uint32_t nstr = (uint32_t)argc;
    memcpy(p, &nstr, 4); p += 4;

    va_list ap;
    va_start(ap, argc);
    for (int i = 0; i < argc; i++) {
        const char *arg = va_arg(ap, const char *);
        uint32_t len = (uint32_t)strlen(arg);
        memcpy(p, &len, 4); p += 4;
        memcpy(p, arg, len); p += len;
    }
    va_end(ap);

    size_t total = p - buf;
    return write_all(fd, buf, total) == (ssize_t)total ? 0 : -1;
}

static void free_reply(struct RamoraReply *rep) {
    for (uint32_t i = 0; i < rep->count && i < 1024; i++) {
        if (rep->items[i].str) {
            free(rep->items[i].str);
            rep->items[i].str = NULL;
        }
    }
    rep->count = 0;
}

static int recv_reply(int fd, struct RamoraReply *rep) {
    memset(rep, 0, sizeof(*rep));

    if (read_all(fd, &rep->code, 1) != 1) return -1;
    if (read_all(fd, &rep->count, 4) != 4) return -1;

    for (uint32_t i = 0; i < rep->count && i < 1024; i++) {
        if (read_all(fd, &rep->items[i].tag, 1) != 1) return -1;

        if (rep->items[i].tag == TAG_NULL) {
            // null item
        } else if (rep->items[i].tag == TAG_INT) {
            if (read_all(fd, &rep->items[i].i32, 4) != 4) return -1;
        } else if (rep->items[i].tag == TAG_INT64) {
            if (read_all(fd, &rep->items[i].i64, 8) != 8) return -1;
        } else if (rep->items[i].tag == TAG_STR) {
            if (read_all(fd, &rep->items[i].len, 4) != 4) return -1;
            rep->items[i].str = malloc(rep->items[i].len + 1);
            if (!rep->items[i].str) return -1;
            if (read_all(fd, rep->items[i].str, rep->items[i].len) != (ssize_t)rep->items[i].len) {
                free(rep->items[i].str);
                rep->items[i].str = NULL;
                return -1;
            }
            rep->items[i].str[rep->items[i].len] = '\0';
        }
    }
    return 0;
}

static int open_connection(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, host, &sa.sin_addr);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static pid_t start_server(const char *conf_path, int port) {
    unlink("./ramora.rdb");
    pid_t pid = fork();
    if (pid == 0) {
        if (conf_path) {
            execl("./ramora-server", "./ramora-server", conf_path, (char*)NULL);
        } else {
            execl("./ramora-server", "./ramora-server", (char*)NULL);
        }
        perror("execl");
        exit(1);
    }

    // Wait for server to become reachable
    for (int i = 0; i < 100; i++) {
        usleep(30000);
        int fd = open_connection("127.0.0.1", port);
        if (fd >= 0) {
            close(fd);
            return pid;
        }
    }
    return pid;
}

static void stop_server(pid_t pid) {
    if (pid > 0) {
        kill(pid, SIGINT);
        int status;
        waitpid(pid, &status, 0);
    }
}

// -------------------------------------------------------------
// Test Functions
// -------------------------------------------------------------

static void test_ping(int fd) {
    TEST_START("PING Command");
    struct RamoraReply rep;
    if (send_cmd(fd, 1, "PING") != 0 || recv_reply(fd, &rep) != 0) {
        TEST_FAIL("network error");
        return;
    }
    if (rep.code == RES_OK && rep.count == 1 && rep.items[0].tag == TAG_STR && strcmp(rep.items[0].str, "PONG") == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected PONG, code=%d, count=%d", rep.code, rep.count);
    }
    free_reply(&rep);
}

static void test_set_get(int fd) {
    TEST_START("SET and GET Operations");
    struct RamoraReply rep;

    // SET key value
    send_cmd(fd, 3, "SET", "hello", "world");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK) {
        TEST_FAIL("SET failed, code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // GET key
    send_cmd(fd, 2, "GET", "hello");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && rep.count == 1 && rep.items[0].tag == TAG_STR && strcmp(rep.items[0].str, "world") == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("GET mismatch: expected 'world', got '%s'", rep.items[0].str ? rep.items[0].str : "NULL");
    }
    free_reply(&rep);
}

static void test_missing_keys(int fd) {
    TEST_START("Missing Key Handling (RES_NX)");
    struct RamoraReply rep;

    send_cmd(fd, 2, "GET", "nonexistent_key_123");
    recv_reply(fd, &rep);
    if (rep.code == RES_NX && rep.count == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("expected RES_NX for missing key, got code=%d", rep.code);
    }
    free_reply(&rep);
}

static void test_del(int fd) {
    TEST_START("DEL Command Restoration & Semantics");
    struct RamoraReply rep;

    // Set then Del
    send_cmd(fd, 3, "SET", "del_target", "to_be_deleted");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 2, "DEL", "del_target");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK) {
        TEST_FAIL("DEL existing key should return RES_OK, got code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // DEL again -> RES_NX
    send_cmd(fd, 2, "DEL", "del_target");
    recv_reply(fd, &rep);
    if (rep.code != RES_NX) {
        TEST_FAIL("DEL missing key should return RES_NX, got code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // GET deleted key -> RES_NX
    send_cmd(fd, 2, "GET", "del_target");
    recv_reply(fd, &rep);
    if (rep.code == RES_NX) {
        TEST_PASS();
    } else {
        TEST_FAIL("GET after DEL should be RES_NX");
    }
    free_reply(&rep);
}

static void test_ttl_and_expiration(int fd) {
    TEST_START("TTL & Expiration (Seconds & Milliseconds)");
    struct RamoraReply rep;

    send_cmd(fd, 3, "SET", "ttl_key_sec", "val1");
    recv_reply(fd, &rep); free_reply(&rep);

    // Set TTL 5 seconds
    send_cmd(fd, 3, "TTL", "ttl_key_sec", "5");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK) {
        TEST_FAIL("TTL set failed, code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // Get TTL sec
    send_cmd(fd, 2, "TTL", "ttl_key_sec");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.count != 1 || rep.items[0].i64 < 3 || rep.items[0].i64 > 5) {
        TEST_FAIL("TTL get sec failed, got %lld", (long long)rep.items[0].i64);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // Short TTL (80ms) and verify expiration
    send_cmd(fd, 3, "SET", "quick_expire", "val2");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 3, "TTLMS", "quick_expire", "80");
    recv_reply(fd, &rep); free_reply(&rep);

    // Immediately check: exists
    send_cmd(fd, 2, "EXISTS", "quick_expire");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.items[0].i32 != 1) {
        TEST_FAIL("quick_expire should exist before timeout");
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // Wait 130ms for expiration
    usleep(130000);

    // Should no longer exist
    send_cmd(fd, 2, "GET", "quick_expire");
    recv_reply(fd, &rep);
    if (rep.code != RES_NX) {
        TEST_FAIL("expired key was returned by GET! code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    TEST_PASS();
}

static void test_stale_ttl_not_delete_newer(int fd) {
    TEST_START("Stale TTL Overwrite Safety");
    struct RamoraReply rep;

    // SET key with short TTL
    send_cmd(fd, 3, "SET", "recycle_key", "old_val");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 3, "TTLMS", "recycle_key", "100");
    recv_reply(fd, &rep); free_reply(&rep);

    // Overwrite key with new value without TTL
    send_cmd(fd, 3, "SET", "recycle_key", "new_val");
    recv_reply(fd, &rep); free_reply(&rep);

    // Wait 150ms for old TTL timestamp to pass
    usleep(150000);

    // Key must still exist with "new_val"
    send_cmd(fd, 2, "GET", "recycle_key");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && rep.count == 1 && strcmp(rep.items[0].str, "new_val") == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("overwritten key was erroneously deleted by stale TTL!");
    }
    free_reply(&rep);
}

static void test_exists(int fd) {
    TEST_START("EXISTS Command (O(1) Check)");
    struct RamoraReply rep;

    send_cmd(fd, 2, "EXISTS", "exists_check_key");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.count != 1 || rep.items[0].i32 != 0) {
        TEST_FAIL("nonexistent key returned %d (code=%d)", rep.items[0].i32, rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    send_cmd(fd, 3, "SET", "exists_check_key", "active");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 2, "EXISTS", "exists_check_key");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && rep.count == 1 && rep.items[0].i32 == 1) {
        TEST_PASS();
    } else {
        TEST_FAIL("existing key returned %d (code=%d)", rep.items[0].i32, rep.code);
    }
    free_reply(&rep);
}

static void test_incr_decr(int fd) {
    TEST_START("INCR and DECR Atomic Arithmetic");
    struct RamoraReply rep;

    // Missing key -> RES_NX according to project conventions
    send_cmd(fd, 2, "INCR", "missing_counter");
    recv_reply(fd, &rep);
    if (rep.code != RES_NX) {
        TEST_FAIL("INCR on missing key expected RES_NX, got code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // SET counter 10
    send_cmd(fd, 3, "SET", "counter", "10");
    recv_reply(fd, &rep); free_reply(&rep);

    // INCR -> 11
    send_cmd(fd, 2, "INCR", "counter");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.count != 1 || rep.items[0].i64 != 11) {
        TEST_FAIL("INCR expected 11, got %lld", (long long)rep.items[0].i64);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // DECR -> 10
    send_cmd(fd, 2, "DECR", "counter");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.count != 1 || rep.items[0].i64 != 10) {
        TEST_FAIL("DECR expected 10, got %lld", (long long)rep.items[0].i64);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // Verify GET matches
    send_cmd(fd, 2, "GET", "counter");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && strcmp(rep.items[0].str, "10") == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("GET counter mismatch: %s", rep.items[0].str);
    }
    free_reply(&rep);
}

static void test_incr_type_error_and_overflow(int fd) {
    TEST_START("INCR/DECR Type Error & Overflow Detection");
    struct RamoraReply rep;

    // Type error on non-integer
    send_cmd(fd, 3, "SET", "string_val", "not_a_number");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 2, "INCR", "string_val");
    recv_reply(fd, &rep);
    if (rep.code != RES_TY) {
        TEST_FAIL("non-numeric INCR expected RES_TY, got code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // Overflow check (INT64_MAX)
    send_cmd(fd, 3, "SET", "max_int", "9223372036854775807");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 2, "INCR", "max_int");
    recv_reply(fd, &rep);
    if (rep.code != RES_ERR) {
        TEST_FAIL("overflow expected RES_ERR, got code=%d", rep.code);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    TEST_PASS();
}

static void test_keys(int fd) {
    TEST_START("KEYS Command with Pattern Matching");
    struct RamoraReply rep;

    send_cmd(fd, 3, "SET", "user:1", "u1"); recv_reply(fd, &rep); free_reply(&rep);
    send_cmd(fd, 3, "SET", "user:2", "u2"); recv_reply(fd, &rep); free_reply(&rep);
    send_cmd(fd, 3, "SET", "post:1", "p1"); recv_reply(fd, &rep); free_reply(&rep);

    // KEYS user:*
    send_cmd(fd, 2, "KEYS", "user:*");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.count != 2) {
        TEST_FAIL("expected 2 keys matching user:*, got count=%u", rep.count);
        free_reply(&rep);
        return;
    }
    free_reply(&rep);

    // KEYS *
    send_cmd(fd, 2, "KEYS", "*");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && rep.count >= 3) {
        TEST_PASS();
    } else {
        TEST_FAIL("KEYS * returned count=%u", rep.count);
    }
    free_reply(&rep);
}

static void test_scan(int fd) {
    TEST_START("SCAN Cursor-Based Iteration");
    struct RamoraReply rep;

    send_cmd(fd, 2, "SCAN", "0");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && rep.count >= 1) {
        // First item is cursor
        TEST_PASS();
    } else {
        TEST_FAIL("SCAN failed: code=%d, count=%u", rep.code, rep.count);
    }
    free_reply(&rep);
}

static void test_info_stats(int fd) {
    TEST_START("INFO / STATS Telemetry");
    struct RamoraReply rep;

    send_cmd(fd, 1, "INFO");
    recv_reply(fd, &rep);
    if (rep.code != RES_OK || rep.count != 1 || !rep.items[0].str) {
        TEST_FAIL("INFO failed: code=%d", rep.code);
        free_reply(&rep);
        return;
    }

    const char* str = rep.items[0].str;
    if (strstr(str, "uptime_in_seconds") &&
        strstr(str, "connected_clients") &&
        strstr(str, "used_memory") &&
        strstr(str, "keyspace_hits") &&
        strstr(str, "keyspace_misses") &&
        strstr(str, "Mission Control Status: Stellar")) {
        TEST_PASS();
    } else {
        TEST_FAIL("missing telemetry fields in INFO output: %s", str);
    }
    free_reply(&rep);
}

static void test_save_command(int fd) {
    TEST_START("SAVE Snapshot Command");
    struct RamoraReply rep;

    send_cmd(fd, 1, "SAVE");
    recv_reply(fd, &rep);
    if (rep.code == RES_OK && access("./ramora.rdb", F_OK) == 0) {
        TEST_PASS();
    } else {
        TEST_FAIL("SAVE command failed or snapshot file missing");
    }
    free_reply(&rep);
}

static void test_pipelining(int fd) {
    TEST_START("Pipelined Batch Requests");
    // Send 10 SET commands back-to-back in one buffer
    uint8_t pipe_buf[4096];
    uint8_t *p = pipe_buf;

    for (int i = 0; i < 10; i++) {
        uint32_t nstr = 3;
        memcpy(p, &nstr, 4); p += 4;
        uint32_t len = 3;
        memcpy(p, &len, 4); p += 4;
        memcpy(p, "SET", 3); p += 3;

        char k[32], v[32];
        snprintf(k, sizeof(k), "pipe_%d", i);
        snprintf(v, sizeof(v), "val_%d", i);
        uint32_t klen = strlen(k), vlen = strlen(v);

        memcpy(p, &klen, 4); p += 4;
        memcpy(p, k, klen); p += klen;

        memcpy(p, &vlen, 4); p += 4;
        memcpy(p, v, vlen); p += vlen;
    }

    write_all(fd, pipe_buf, p - pipe_buf);

    int ok_count = 0;
    for (int i = 0; i < 10; i++) {
        struct RamoraReply rep;
        if (recv_reply(fd, &rep) == 0 && rep.code == RES_OK) {
            ok_count++;
        }
        free_reply(&rep);
    }

    if (ok_count == 10) {
        TEST_PASS();
    } else {
        TEST_FAIL("pipelined operations failed: %d/10 succeeded", ok_count);
    }
}

// -------------------------------------------------------------
// Advanced Scenarios: Persistence Reload, Eviction, AUTH
// -------------------------------------------------------------

static void test_persistence_reload_scenario(void) {
    TEST_START("Persistence Reload & TTL Preservation on Restart");

    // Start server on port 5555
    const char *test_conf = "conf/test_persist.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "bind 127.0.0.1\nport 5555\nsnapshot_path ./test_snapshot.rdb\nlogfile ./test_persist.log\n");
    fclose(fp);
    unlink("./test_snapshot.rdb");

    pid_t pid = start_server(test_conf, 5555);
    int fd = open_connection("127.0.0.1", 5555);
    struct RamoraReply rep;

    // Insert keys: one persistent, one with long TTL (10s)
    send_cmd(fd, 3, "SET", "persist_foo", "bar_val");
    recv_reply(fd, &rep); free_reply(&rep);

    send_cmd(fd, 3, "SET", "persist_ttl", "ttl_val");
    recv_reply(fd, &rep); free_reply(&rep);
    send_cmd(fd, 3, "TTL", "persist_ttl", "15");
    recv_reply(fd, &rep); free_reply(&rep);

    // Save snapshot
    send_cmd(fd, 1, "SAVE");
    recv_reply(fd, &rep); free_reply(&rep);
    close(fd);

    // Stop server
    stop_server(pid);

    // Start new server instance using same snapshot
    pid = start_server(test_conf, 5555);
    fd = open_connection("127.0.0.1", 5555);

    // Verify persistent key restored
    send_cmd(fd, 2, "GET", "persist_foo");
    recv_reply(fd, &rep);
    int ok = (rep.code == RES_OK && rep.count == 1 && strcmp(rep.items[0].str, "bar_val") == 0);
    free_reply(&rep);

    // Verify TTL key restored with remaining TTL
    send_cmd(fd, 2, "TTL", "persist_ttl");
    recv_reply(fd, &rep);
    ok &= (rep.code == RES_OK && rep.count == 1 && rep.items[0].i64 > 5);
    free_reply(&rep);

    close(fd);
    stop_server(pid);
    unlink("./test_snapshot.rdb");
    unlink(test_conf);
    unlink("./test_persist.log");

    if (ok) TEST_PASS();
    else TEST_FAIL("snapshot reload failed to preserve keys or TTL");
}

static void test_corrupted_snapshot_handling(void) {
    TEST_START("Corrupted Snapshot Recovery Safety");

    const char *test_conf = "conf/test_corrupt.conf";
    const char *snap_file = "./test_corrupt.rdb";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "bind 127.0.0.1\nport 5556\nsnapshot_path %s\nlogfile ./test_corrupt.log\n", snap_file);
    fclose(fp);

    // Write a corrupted snapshot file (truncated after magic)
    FILE *sf = fopen(snap_file, "wb");
    fwrite("RAMORA_V1", 1, 9, sf);
    fwrite("GARBAGE_TRUNCATED_DATA_12345", 1, 28, sf);
    fclose(sf);

    // Start server — must handle gracefully, not crash
    pid_t pid = start_server(test_conf, 5556);
    int fd = open_connection("127.0.0.1", 5556);

    int ok = 0;
    if (fd >= 0) {
        struct RamoraReply rep;
        if (send_cmd(fd, 1, "PING") == 0 && recv_reply(fd, &rep) == 0 && rep.code == RES_OK) {
            ok = 1;
        }
        free_reply(&rep);
        close(fd);
    }

    stop_server(pid);
    unlink(snap_file);
    unlink(test_conf);
    unlink("./test_corrupt.log");

    if (ok) TEST_PASS();
    else TEST_FAIL("server crashed or failed to boot on corrupted snapshot");
}

static void test_maxmemory_eviction(void) {
    TEST_START("Maxmemory & LRU Eviction Under Limit");

    const char *test_conf = "conf/test_evict.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "bind 127.0.0.1\nport 5557\nmaxmemory 65536\nmaxmemory_policy lru\nlogfile ./test_evict.log\n");
    fclose(fp);

    pid_t pid = start_server(test_conf, 5557);
    int fd = open_connection("127.0.0.1", 5557);
    struct RamoraReply rep;

    // Fill with entries to trigger LRU eviction
    for (int i = 0; i < 200; i++) {
        char k[32], v[256];
        snprintf(k, sizeof(k), "k:%d", i);
        memset(v, 'A', sizeof(v) - 1);
        v[sizeof(v) - 1] = '\0';
        send_cmd(fd, 3, "SET", k, v);
        recv_reply(fd, &rep);
        free_reply(&rep);
    }

    // Server should remain alive and responsive
    send_cmd(fd, 1, "PING");
    recv_reply(fd, &rep);
    int ok = (rep.code == RES_OK);
    free_reply(&rep);

    // Verify recent key exists
    send_cmd(fd, 2, "EXISTS", "k:199");
    recv_reply(fd, &rep);
    ok &= (rep.code == RES_OK && rep.items[0].i32 == 1);
    free_reply(&rep);

    // Verify earliest keys were evicted
    send_cmd(fd, 2, "EXISTS", "k:0");
    recv_reply(fd, &rep);
    ok &= (rep.code == RES_OK && rep.items[0].i32 == 0);
    free_reply(&rep);

    close(fd);
    stop_server(pid);
    unlink(test_conf);
    unlink("./test_evict.log");

    if (ok) TEST_PASS();
    else TEST_FAIL("eviction did not properly cap memory or evict oldest items");
}

static void test_auth(void) {
    TEST_START("AUTH Configuration & Connection Protection");

    const char *test_conf = "conf/test_auth.conf";
    FILE *fp = fopen(test_conf, "w");
    fprintf(fp, "bind 127.0.0.1\nport 5558\nrequirepass secretpass123\nlogfile ./test_auth.log\n");
    fclose(fp);

    pid_t pid = start_server(test_conf, 5558);
    int fd = open_connection("127.0.0.1", 5558);
    struct RamoraReply rep;

    // Try command before auth -> RES_ERR
    send_cmd(fd, 1, "PING");
    recv_reply(fd, &rep);
    int ok = (rep.code == RES_ERR);
    free_reply(&rep);

    // Try wrong password -> RES_ERR
    send_cmd(fd, 2, "AUTH", "wrongpass");
    recv_reply(fd, &rep);
    ok &= (rep.code == RES_ERR);
    free_reply(&rep);

    // Correct password -> RES_OK
    send_cmd(fd, 2, "AUTH", "secretpass123");
    recv_reply(fd, &rep);
    ok &= (rep.code == RES_OK);
    free_reply(&rep);

    // Now PING succeeds
    send_cmd(fd, 1, "PING");
    recv_reply(fd, &rep);
    ok &= (rep.code == RES_OK && strcmp(rep.items[0].str, "PONG") == 0);
    free_reply(&rep);

    close(fd);
    stop_server(pid);
    unlink(test_conf);
    unlink("./test_auth.log");

    if (ok) TEST_PASS();
    else TEST_FAIL("AUTH enforcement failed");
}

int main(void) {
    printf("\n=======================================================\n");
    printf("   RAMORA IN-MEMORY CACHE — COMPREHENSIVE TEST SUITE   \n");
    printf("=======================================================\n\n");

    int port = 5432;
    const char *conf = "conf/test_main.conf";
    FILE *fp = fopen(conf, "w");
    fprintf(fp, "bind 127.0.0.1\nport %d\nlogfile ./test_main.log\n", port);
    fclose(fp);

    pid_t pid = start_server(conf, port);
    if (pid < 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    int fd = open_connection("127.0.0.1", port);
    if (fd < 0) {
        fprintf(stderr, "Failed to connect to server on port %d\n", port);
        stop_server(pid);
        return 1;
    }

    // Core functionality tests
    test_ping(fd);
    test_set_get(fd);
    test_missing_keys(fd);
    test_del(fd);
    test_ttl_and_expiration(fd);
    test_stale_ttl_not_delete_newer(fd);
    test_exists(fd);
    test_incr_decr(fd);
    test_incr_type_error_and_overflow(fd);
    test_keys(fd);
    test_scan(fd);
    test_info_stats(fd);
    test_save_command(fd);
    test_pipelining(fd);

    close(fd);
    stop_server(pid);
    unlink(conf);
    unlink("./test_main.log");

    // Advanced integration tests
    test_persistence_reload_scenario();
    test_corrupted_snapshot_handling();
    test_maxmemory_eviction();
    test_auth();

    printf("\n=======================================================\n");
    printf("  TEST SUMMARY: %d Run, %d Passed, %d Failed\n", g_tests_run, g_tests_passed, g_tests_failed);
    printf("=======================================================\n\n");

    return g_tests_failed == 0 ? 0 : 1;
}
