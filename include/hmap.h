#ifndef HMAP_H
#define HMAP_H

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>

#include "buf.h"
#include "dlist.h"

struct HNode {
    struct HNode* next;
    uint64_t hcode;
};

struct HTab {
    struct HNode** tab;
    size_t cap;
    size_t size;
};

struct HMap {
    struct HTab* new_tab;
    struct HTab* old_tab;
    uint8_t is_migrating;
    uint32_t migrating_pos;
};

int init_HMap(struct HMap* hmap, size_t cap);
int insert_HMap(struct HMap* hmap, struct HNode* node);
struct HNode** lookup_HMap(struct HMap* hmap, const char* key, uint64_t hcode_key, int (*eq)(const char* key, struct HNode*));
struct HNode* delete_HMap(struct HMap* hmap, const char* key, uint64_t hcode_key, int (*eq)(const char* key, struct HNode*));
void free_HMap(struct HMap* hmap);
size_t hmap_size(struct HMap* hmap);

// ENTRY HEADER

struct Entry {
    struct HNode node;
    struct buf* key;
    struct buf* val;

    size_t heap_idx;
    struct DList lru_node;
};

int init_Entry(struct Entry* entry);
void free_Entry(struct Entry* entry);
int entry_eq(const char* key, struct HNode* node);
uint64_t str_hash(const char *data, size_t len);

void hmap_for_each(struct HMap* hmap, void (*cb)(struct Entry* entry, void* arg), void* arg);
uint64_t hmap_scan(struct HMap* hmap, uint64_t cursor, size_t count, void (*cb)(struct Entry* entry, void* arg), void* arg);

#endif