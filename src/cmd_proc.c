#include "cmd_proc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

const char* cosmo_polo_telemetry(void){
    return "Mission Control Status: Stellar";
}

struct CommandSpec command_list[] = {
    {"PING", 1, handle_ping},
    {"GET", 2, handle_get},
    {"SET", 3, handle_set},
    {"DEL", 2, handle_del},
    {"TTL", 3, handle_set_ttl_sec},
    {"TTL", 2, handle_get_ttl_sec},
    {"TTLMS", 3, handle_set_ttl_ms},
    {"TTLMS", 2, handle_get_ttl_ms},
    {"EXISTS", 2, handle_exists},
    {"INCR", 2, handle_incr},
    {"DECR", 2, handle_decr},
    {"KEYS", 2, handle_keys},
    {"KEYS", 1, handle_keys},
    {"SCAN", 3, handle_scan},
    {"SCAN", 2, handle_scan},
    {"SCAN", 1, handle_scan},
    {"AUTH", 2, handle_auth},
    {"INFO", 1, handle_info},
    {"INFO", 2, handle_info},
    {"STATS", 1, handle_info},
    {"STATS", 2, handle_info},
    {"SAVE", 1, handle_save}
};

size_t command_list_len = sizeof(command_list) / sizeof(command_list[0]);

char* get_string(struct buf* buffer, size_t* offset){
    uint32_t len;
    bufcpylenoffset(buffer, *offset, &len, HEADER_SIZE);

    char* str = malloc(sizeof(char) * (len + 1));
    if (str == NULL){
        die("get_string (cmd_proc.c): malloc");
        exit(EXIT_FAILURE);
    }
    *offset += HEADER_SIZE;
    bufcpylenoffset(buffer, *offset, str, len);
    str[len] = '\0';
    *offset += len;

    return str;
}

static int64_t parse_to_int(char* str){
    int is_neg = 0;
    if (*str == '-'){
        is_neg = 1;
        str++;
    }
    int64_t val = 0;
    if (!*str) return -2;
    while (*str){
        if (*str < '0' || *str > '9'){
            return -2; // -2 for incorrect type
        }

        val *= 10;
        val += *str - '0';
        str++;
    }

    if (is_neg == 1){
        val *= -1;
    }

    if (val < 0) return -1;
    else return val;
}

int handle_ping(struct Conn* conn, size_t offset){
    (void)offset;
    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_str(conn->wbuf, "PONG");
    return rv;
}

int handle_get(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);

    struct buf* val = get_Entry(key);
    free(key);
    
    if (val == NULL){
        int rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
        return rv;
    }

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_str_len(conn->wbuf, bufstart(val), bufsize(val));
    return rv;
}

int handle_set(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    char* val = get_string(conn->rbuf, &offset);
    int rv = set_Entry(key, val);
    free(key);
    free(val);

    if (rv == 0) {
        rv = response_code(conn->wbuf, RES_ERR);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
    }
    rv &= response_u32(conn->wbuf, 0);
    return rv;
}

int handle_del(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int rv = del_Entry(key);
    free(key);

    int res = 1;
    if (rv == 0){
        res = response_code(conn->wbuf, RES_NX);
        res &= response_u32(conn->wbuf, 0);
    }
    else {
        res = response_code(conn->wbuf, RES_OK);
        res &= response_u32(conn->wbuf, 0);
    }
    return res;
}

int handle_exists(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int exists = exists_Entry(key);
    free(key);

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_int(conn->wbuf, exists ? 1 : 0);
    return rv;
}

int handle_incr(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int64_t new_val = 0;
    int res = incr_decr_Entry(key, 1, &new_val);
    free(key);

    int rv = 1;
    if (res == RES_NX){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == RES_TY){
        rv = response_code(conn->wbuf, RES_TY);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == RES_ERR){
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 1);
        rv &= response_int64(conn->wbuf, new_val);
    }
    return rv;
}

int handle_decr(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int64_t new_val = 0;
    int res = incr_decr_Entry(key, -1, &new_val);
    free(key);

    int rv = 1;
    if (res == RES_NX){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == RES_TY){
        rv = response_code(conn->wbuf, RES_TY);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == RES_ERR){
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 1);
        rv &= response_int64(conn->wbuf, new_val);
    }
    return rv;
}

struct KeysCollector {
    char** keys;
    size_t count;
    size_t cap;
    const char* pattern;
    uint64_t now_ms;
};

static void keys_cb(struct Entry* entry, void* arg){
    struct KeysCollector* col = (struct KeysCollector*)arg;
    if (entry->heap_idx != (size_t)-1 && gd.ttl_heap.arr[entry->heap_idx].expiration_time <= col->now_ms){
        return;
    }
    const char* kstr = bufstart(entry->key);
    if (glob_match(col->pattern, kstr)){
        if (col->count >= col->cap){
            col->cap *= 2;
            col->keys = realloc(col->keys, col->cap * sizeof(char*));
        }
        col->keys[col->count++] = strdup(kstr);
    }
}

int handle_keys(struct Conn* conn, size_t offset){
    uint32_t nstr = 0;
    bufcpylen(conn->rbuf, &nstr, HEADER_SIZE);

    char* pattern = NULL;
    if (nstr >= 2){
        pattern = get_string(conn->rbuf, &offset);
    } else {
        pattern = strdup("*");
    }

    struct KeysCollector col = {
        .keys = malloc(16 * sizeof(char*)),
        .count = 0,
        .cap = 16,
        .pattern = pattern,
        .now_ms = get_monotonic_msec()
    };

    hmap_for_each(&gd.kv_db, keys_cb, &col);

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, (uint32_t)col.count);
    for (size_t i = 0; i < col.count; ++i){
        rv &= response_str(conn->wbuf, col.keys[i]);
        free(col.keys[i]);
    }
    free(col.keys);
    free(pattern);
    return rv;
}

int handle_scan(struct Conn* conn, size_t offset){
    uint32_t nstr = 0;
    bufcpylen(conn->rbuf, &nstr, HEADER_SIZE);

    uint64_t cursor = 0;
    char* pattern = NULL;

    if (nstr >= 2){
        char* cursor_str = get_string(conn->rbuf, &offset);
        int64_t c = parse_to_int(cursor_str);
        if (c >= 0) cursor = (uint64_t)c;
        free(cursor_str);
    }
    if (nstr >= 3){
        pattern = get_string(conn->rbuf, &offset);
    } else {
        pattern = strdup("*");
    }

    struct KeysCollector col = {
        .keys = malloc(16 * sizeof(char*)),
        .count = 0,
        .cap = 16,
        .pattern = pattern,
        .now_ms = get_monotonic_msec()
    };

    uint64_t next_cursor = hmap_scan(&gd.kv_db, cursor, 20, keys_cb, &col);

    char next_cursor_str[32];
    snprintf(next_cursor_str, sizeof(next_cursor_str), "%llu", (unsigned long long)next_cursor);

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, (uint32_t)(1 + col.count));
    rv &= response_str(conn->wbuf, next_cursor_str);
    for (size_t i = 0; i < col.count; ++i){
        rv &= response_str(conn->wbuf, col.keys[i]);
        free(col.keys[i]);
    }
    free(col.keys);
    free(pattern);
    return rv;
}

int handle_auth(struct Conn* conn, size_t offset){
    char* pass = get_string(conn->rbuf, &offset);
    int rv = 1;
    if (requirepass[0] == '\0'){
        conn->authenticated = 1;
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (strcmp(pass, requirepass) == 0){
        conn->authenticated = 1;
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        conn->authenticated = 0;
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    free(pass);
    return rv;
}

int handle_info(struct Conn* conn, size_t offset){
    (void)offset;
    time_t now = time(NULL);
    uint64_t uptime = (uint64_t)(now >= (time_t)gd.start_time_sec ? now - (time_t)gd.start_time_sec : 0);
    size_t keys_count = hmap_size(&gd.kv_db);

    char info_buf[2048];
    snprintf(info_buf, sizeof(info_buf),
        "# Server\r\n"
        "ramora_version:0.2.0\r\n"
        "uptime_in_seconds:%llu\r\n"
        "\r\n"
        "# Clients\r\n"
        "connected_clients:%d\r\n"
        "\r\n"
        "# Memory\r\n"
        "used_memory:%zu\r\n"
        "maxmemory:%zu\r\n"
        "maxmemory_policy:%s\r\n"
        "\r\n"
        "# Stats\r\n"
        "total_connections_received:%llu\r\n"
        "total_commands_processed:%llu\r\n"
        "keyspace_hits:%llu\r\n"
        "keyspace_misses:%llu\r\n"
        "\r\n"
        "# Keyspace\r\n"
        "keys:%zu\r\n"
        "expires:%zu\r\n"
        "\r\n"
        "# Telemetry\r\n"
        "mission_control:%s\r\n",
        (unsigned long long)uptime,
        gd.connected_clients,
        gd.used_memory,
        MAX_MEMORY,
        MAXMEMORY_POLICY,
        (unsigned long long)gd.stat_total_connections,
        (unsigned long long)gd.stat_total_commands,
        (unsigned long long)gd.stat_hits,
        (unsigned long long)gd.stat_misses,
        keys_count,
        gd.ttl_heap.sz,
        cosmo_polo_telemetry()
    );

    int rv = response_code(conn->wbuf, RES_OK);
    rv &= response_u32(conn->wbuf, 1);
    rv &= response_str(conn->wbuf, info_buf);
    return rv;
}

int handle_save(struct Conn* conn, size_t offset){
    (void)offset;
    int rv = save_snapshot(SNAPSHOT_PATH);
    int res = response_code(conn->wbuf, rv ? RES_OK : RES_ERR);
    res &= response_u32(conn->wbuf, 0);
    return res;
}

int handle_get_ttl_sec(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int64_t ttl = get_ttl_Entry_sec(key);
    free(key);

    int rv = 1;
    if (ttl == -2){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 1);
        rv &= response_int64(conn->wbuf, ttl);
    }
    return rv;
}

int handle_get_ttl_ms(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    int64_t ttl = get_ttl_Entry_ms(key);
    free(key);

    int rv = 1;
    if (ttl == -2){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 1);
        rv &= response_int64(conn->wbuf, ttl);
    }
    return rv;
}

int handle_set_ttl_sec(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    char* ttl_string = get_string(conn->rbuf, &offset);
    int64_t ttl = parse_to_int(ttl_string);

    if (ttl == -2){
        int rv = 1;
        rv = response_code(conn->wbuf, RES_TY);
        rv &= response_u32(conn->wbuf, 0);
        free(key);
        free(ttl_string);
        return rv;
    }

    int res = set_ttl_Entry_sec(key, ttl);
    free(key);
    free(ttl_string);

    int rv = 1;
    if (res == -1){
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == 0){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 0);
    }
    return rv;
}

int handle_set_ttl_ms(struct Conn* conn, size_t offset){
    char* key = get_string(conn->rbuf, &offset);
    char* ttl_string = get_string(conn->rbuf, &offset);
    int64_t ttl = parse_to_int(ttl_string);

    if (ttl == -2){
        int rv = 1;
        rv = response_code(conn->wbuf, RES_TY);
        rv &= response_u32(conn->wbuf, 0);
        free(key);
        free(ttl_string);
        return rv;
    }

    int res = set_ttl_Entry_ms(key, ttl);
    free(key);
    free(ttl_string);

    int rv = 1;
    if (res == -1){
        rv = response_code(conn->wbuf, RES_ERR);
        rv &= response_u32(conn->wbuf, 0);
    }
    else if (res == 0){
        rv = response_code(conn->wbuf, RES_NX);
        rv &= response_u32(conn->wbuf, 0);
    }
    else {
        rv = response_code(conn->wbuf, RES_OK);
        rv &= response_u32(conn->wbuf, 0);
    }
    return rv;
}