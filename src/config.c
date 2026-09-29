#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

char logfile_path[1024] = "/var/log/ramora/ramora.log";

char bind_addr[64] = "127.0.0.1";
int port = 5000;

float MAX_LOAD_FACTOR = 0.75;
int MIGRATING_LOAD = 5;
int INITIAL_HMAP_CAP = 65536;

int INITIAL_HEAP_CAP = 1024;
int INITIAL_BUFFER_CAP = 8192;
int MAX_BUFFER_CAP = 8388608;

int MAX_PAYLOAD_SIZE = 4096;
int MAX_POOLED_CONN_COUNT = 64;
int IDLE_TIMEOUT_MS = 10000;

int READ_CHUNK = 8192;
int MAX_EVENTS = 1024;

char requirepass[256] = "";
size_t MAX_MEMORY = 0;
char MAXMEMORY_POLICY[32] = "lru";
char SNAPSHOT_PATH[1024] = "./ramora.rdb";
int SAVE_INTERVAL_SEC = 0;

static size_t parse_memory_size(const char* val) {
    char* end = NULL;
    double num = strtod(val, &end);
    if (num <= 0) return 0;
    if (end && *end) {
        if (*end == 'k' || *end == 'K') num *= 1024;
        else if (*end == 'm' || *end == 'M') num *= 1024 * 1024;
        else if (*end == 'g' || *end == 'G') num *= 1024ULL * 1024 * 1024;
    }
    return (size_t)num;
}

int load_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        msg("config file not found, using the default values");
        return 1;
    }

    char line[512];
    char key[64], val[256];

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        if (sscanf(line, "%63s %255s", key, val) != 2)
            continue;
        
        if (!strcmp(key, "bind")) {
            snprintf(bind_addr, sizeof(bind_addr), "%.63s", val);
        }
        else if (!strcmp(key, "port")) {
            int p = atoi(val);
            if (p <= 0 || p > 65535) {
                msg("Invalid port in config file, using default port: \'5000\'");
            }
            else port = p;
        }

        else if (!strcmp(key, "logfile")) {
            snprintf(logfile_path, sizeof(logfile_path), "%s", val);
        }

        else if (!strcmp(key, "max_load_factor")) MAX_LOAD_FACTOR = strtof(val, NULL);
        else if (!strcmp(key, "migrating_load")) MIGRATING_LOAD = atoi(val);
        else if (!strcmp(key, "initial_hmap_cap")) INITIAL_HMAP_CAP = atoi(val);

        else if (!strcmp(key, "initial_heap_cap")) INITIAL_HEAP_CAP = atoi(val);
        else if (!strcmp(key, "initial_buffer_cap")) INITIAL_BUFFER_CAP = atoi(val);
        else if (!strcmp(key, "max_buffer_cap")) MAX_BUFFER_CAP = atoi(val);

        else if (!strcmp(key, "max_payload_size")) MAX_PAYLOAD_SIZE = atoi(val);
        else if (!strcmp(key, "max_pooled_conn_count")) MAX_POOLED_CONN_COUNT = atoi(val);
        else if (!strcmp(key, "idle_timeout_ms")) IDLE_TIMEOUT_MS = atoi(val);

        else if (!strcmp(key, "read_chunk")) READ_CHUNK = atoi(val);
        else if (!strcmp(key, "max_events")) MAX_EVENTS = atoi(val);

        else if (!strcmp(key, "requirepass") || !strcmp(key, "password")) {
            snprintf(requirepass, sizeof(requirepass), "%s", val);
        }
        else if (!strcmp(key, "maxmemory")) {
            MAX_MEMORY = parse_memory_size(val);
        }
        else if (!strcmp(key, "maxmemory_policy")) {
            snprintf(MAXMEMORY_POLICY, sizeof(MAXMEMORY_POLICY), "%.31s", val);
        }
        else if (!strcmp(key, "snapshot_path") || !strcmp(key, "dbfilename")) {
            snprintf(SNAPSHOT_PATH, sizeof(SNAPSHOT_PATH), "%s", val);
        }
        else if (!strcmp(key, "save_interval_sec")) {
            SAVE_INTERVAL_SEC = atoi(val);
        }
    }

    fclose(fp);
    return 0;
}