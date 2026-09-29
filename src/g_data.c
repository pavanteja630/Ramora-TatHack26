#include "g_data.h"
#include <time.h>

int init_g_data(struct g_data* gd){
    int rv = init_HMap(&gd->kv_db, INITIAL_HMAP_CAP);
    if (rv == 0){
        die("init_g_data (g_data.c): cannot initialize hmap");
        return 0;
    }

    dlist_init(&gd->idle_list);
    rv &= heap_init(&gd->ttl_heap, INITIAL_HEAP_CAP);
    if (rv == 0){
        free_HMap(&gd->kv_db);
        die("init_g_data (g_data.c): cannot initialize heap");
        return 0;
    }
    
    dlist_init(&gd->conn_pool);
    gd->pooled_conn_count = 0;
    dlist_init(&gd->lru_list);
    gd->used_memory = 0;
    gd->connected_clients = 0;
    gd->stat_hits = 0;
    gd->stat_misses = 0;
    gd->stat_total_connections = 0;
    gd->stat_total_commands = 0;
    gd->start_time_sec = (uint64_t)time(NULL);
    return 1;
}