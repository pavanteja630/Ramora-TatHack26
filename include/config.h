#ifndef CONFIG_H
#define CONFIG_H

#include "logging_helper.h"

extern char logfile_path[1024];

extern char bind_addr[64];
extern int port;

extern float MAX_LOAD_FACTOR;
extern int MIGRATING_LOAD;
extern int INITIAL_HMAP_CAP;

extern int INITIAL_HEAP_CAP;
extern int INITIAL_BUFFER_CAP;
extern int MAX_BUFFER_CAP;

extern int MAX_PAYLOAD_SIZE;
extern int MAX_POOLED_CONN_COUNT;
extern int IDLE_TIMEOUT_MS;

extern int READ_CHUNK;
extern int MAX_EVENTS;

extern char requirepass[256];
extern size_t MAX_MEMORY;
extern char MAXMEMORY_POLICY[32];
extern char SNAPSHOT_PATH[1024];
extern int SAVE_INTERVAL_SEC;

int load_config(const char *path);

#endif
