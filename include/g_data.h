#ifndef G_DATA_H
#define G_DATA_H

#include "hmap.h"
#include "dlist.h"
#include "heap.h"

struct g_data {
    struct HMap kv_db;
    struct DList idle_list;
    struct Heap ttl_heap;
    struct DList conn_pool;
    int pooled_conn_count;
    struct DList lru_list;
    size_t used_memory;
    int connected_clients;
    uint64_t stat_hits;
    uint64_t stat_misses;
    uint64_t stat_total_connections;
    uint64_t stat_total_commands;
    uint64_t start_time_sec;
};

int init_g_data(struct g_data* gd);

#endif