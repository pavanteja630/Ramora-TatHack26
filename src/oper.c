#include "oper.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

#define container_of(ptr, T, member) \
    ((T *)( (char *)ptr - offsetof(T, member) ))

#define SNAPSHOT_MAGIC "RAMORA_V1"
#define SNAPSHOT_EOF_MAGIC "RAMORA_EOF"

int glob_match(const char* pat, const char* str) {
    while (*pat) {
        if (*pat == '*') {
            while (*(pat + 1) == '*') pat++;
            if (*(pat + 1) == '\0') return 1;
            while (*str) {
                if (glob_match(pat + 1, str)) return 1;
                str++;
            }
            return 0;
        } else if (*pat == '?') {
            if (*str == '\0') return 0;
            pat++;
            str++;
        } else {
            if (*pat != *str) return 0;
            pat++;
            str++;
        }
    }
    return *str == '\0';
}

int evict_if_needed(size_t needed) {
    if (MAX_MEMORY == 0) return 1;
    if (needed > MAX_MEMORY) return 0;

    while (gd.used_memory + needed > MAX_MEMORY && !dlist_empty(&gd.lru_list)) {
        struct Entry* victim = (struct Entry*)container_of(gd.lru_list.next, struct Entry, lru_node);
        del_Entry(bufstart(victim->key));
    }

    return (gd.used_memory + needed <= MAX_MEMORY);
}

int set_ttl_Entry_ms(char* key, int64_t ttl){
    if (ttl < -1) return -1;

    struct HMap* hmap = &gd.kv_db;
    uint64_t hcode_key = str_hash(key, strlen(key));
    struct HNode** from = lookup_HMap(hmap, key, hcode_key, entry_eq);

    if (from == NULL || *from == NULL){
        return 0;
    }

    struct Entry* entry = (struct Entry*)container_of(*from, struct Entry, node);

    // If key has already expired, delete it and return 0 (not found)
    if (entry->heap_idx != (size_t)-1 && gd.ttl_heap.arr[entry->heap_idx].expiration_time <= get_monotonic_msec()){
        del_Entry(key);
        return 0;
    }

    if (ttl == -1){
        if (entry->heap_idx != (size_t)-1){
            heap_delete(&gd.ttl_heap, entry->heap_idx);
            entry->heap_idx = (size_t)-1;
        }
        return 1;
    }

    uint64_t expiration_time = get_monotonic_msec() + (uint64_t)ttl;

    if (entry->heap_idx == (size_t)-1){
        if (heap_insert(&gd.ttl_heap, expiration_time, &entry->heap_idx) == 0){
            return -1;
        }
    }
    else {
        gd.ttl_heap.arr[entry->heap_idx].expiration_time = expiration_time;
        heap_update(gd.ttl_heap.arr, entry->heap_idx, gd.ttl_heap.sz);
    }
    return 1;
}

int64_t get_ttl_Entry_ms(char* key){
    struct HMap* hmap = &gd.kv_db;

    uint64_t hcode_key = str_hash(key, strlen(key));
    struct HNode** from = lookup_HMap(hmap, key, hcode_key, entry_eq);

    if (from == NULL || *from == NULL){
        return -2; // -2 for RES_NX -> key not present
    }

    struct Entry* entry = (struct Entry*)container_of(*from, struct Entry, node);
    if (entry->heap_idx == (size_t)-1){
        return -1; // -1 for no ttl -> not set for expiration
    }

    uint64_t now = get_monotonic_msec();
    if (gd.ttl_heap.arr[entry->heap_idx].expiration_time <= now){
        del_Entry(key);
        return -2; // expired -> RES_NX
    }

    return (int64_t)(gd.ttl_heap.arr[entry->heap_idx].expiration_time - now);
}

int set_ttl_Entry_sec(char* key, int64_t ttl){
    if (ttl == -1) return set_ttl_Entry_ms(key, -1);
    if (ttl < 0) return -1;
    if (ttl > INT64_MAX / 1000) return -1;
    return set_ttl_Entry_ms(key, ttl * 1000);
}

int64_t get_ttl_Entry_sec(char* key){
    int64_t ttl = get_ttl_Entry_ms(key);
    if (ttl < 0) return ttl;
    else return ttl / 1000;
}

struct buf* get_Entry(char* key){
    struct HMap* hmap = &gd.kv_db;

    uint64_t hcode_key = str_hash(key, strlen(key));
    struct HNode** from = lookup_HMap(hmap, key, hcode_key, entry_eq);

    if (from == NULL || *from == NULL){
        gd.stat_misses++;
        return NULL;
    }

    struct Entry* entry = container_of(*from, struct Entry, node);

    // Passive/lazy expiration check
    if (entry->heap_idx != (size_t)-1 && gd.ttl_heap.arr[entry->heap_idx].expiration_time <= get_monotonic_msec()){
        del_Entry(key);
        gd.stat_misses++;
        return NULL;
    }

    // Touch LRU
    dlist_delete(&entry->lru_node);
    dlist_insert_before(&gd.lru_list, &entry->lru_node);

    gd.stat_hits++;
    return entry->val;
}

int exists_Entry(char* key){
    struct HMap* hmap = &gd.kv_db;

    uint64_t hcode_key = str_hash(key, strlen(key));
    struct HNode** from = lookup_HMap(hmap, key, hcode_key, entry_eq);

    if (from == NULL || *from == NULL){
        gd.stat_misses++;
        return 0;
    }

    struct Entry* entry = container_of(*from, struct Entry, node);

    // Passive/lazy expiration check
    if (entry->heap_idx != (size_t)-1 && gd.ttl_heap.arr[entry->heap_idx].expiration_time <= get_monotonic_msec()){
        del_Entry(key);
        gd.stat_misses++;
        return 0;
    }

    // Touch LRU
    dlist_delete(&entry->lru_node);
    dlist_insert_before(&gd.lru_list, &entry->lru_node);

    gd.stat_hits++;
    return 1;
}

int set_Entry(char* key, char* val){
    struct HMap* hmap = &gd.kv_db;

    size_t key_len = strlen(key);
    size_t val_len = strlen(val);
    uint64_t hcode_key = str_hash(key, key_len);
    struct HNode** from = lookup_HMap(hmap, key, hcode_key, entry_eq);

    if (from != NULL && *from != NULL){
        struct Entry* found_entry = container_of(*from, struct Entry, node);

        // If the key already had a TTL, SET removes the old expiration!
        if (found_entry->heap_idx != (size_t)-1){
            heap_delete(&gd.ttl_heap, found_entry->heap_idx);
            found_entry->heap_idx = (size_t)-1;
        }

        // Memory check for value update
        size_t old_cap = bufcap(found_entry->val);
        size_t est_new_cap = (val_len > old_cap) ? val_len * 2 : old_cap;
        size_t delta_mem = (est_new_cap > old_cap) ? (est_new_cap - old_cap) : 0;
        if (!evict_if_needed(delta_mem)){
            return 0;
        }

        clearbuf(found_entry->val);
        int rv = bufappend(found_entry->val, val, val_len);
        if (rv == 0) return 0;

        size_t new_cap = bufcap(found_entry->val);
        if (new_cap > old_cap) gd.used_memory += (new_cap - old_cap);
        else if (old_cap > new_cap) gd.used_memory -= (old_cap - new_cap);

        // Touch LRU
        dlist_delete(&found_entry->lru_node);
        dlist_insert_before(&gd.lru_list, &found_entry->lru_node);
        return 1;
    }

    // New entry memory check
    size_t est_entry_mem = sizeof(struct Entry) + (key_len + 1) + val_len + 128;
    if (!evict_if_needed(est_entry_mem)){
        return 0;
    }

    struct Entry* entry = malloc(sizeof(struct Entry));
    if (entry == NULL){
        msg("set_Entry (oper.c): malloc");
        return 0;
    }
    if (init_Entry(entry) == 0){
        free(entry);
        return 0;
    }

    int rv = 1;
    rv &= bufappend(entry->key, key, key_len + 1);
    rv &= bufappend(entry->val, val, val_len);
    if (rv == 0){
        free_Entry(entry);
        free(entry);
        return 0;
    }

    entry->node.hcode = hcode_key;
    if (insert_HMap(hmap, &entry->node) == 0){
        free_Entry(entry);
        free(entry);
        return 0;
    }

    dlist_insert_before(&gd.lru_list, &entry->lru_node);
    gd.used_memory += (sizeof(struct Entry) + bufcap(entry->key) + bufcap(entry->val));

    return 1;
}

int del_Entry(char* key){
    struct HMap* hmap = &gd.kv_db;

    uint64_t hcode_key = str_hash(key, strlen(key));
    struct HNode* node = delete_HMap(hmap, key, hcode_key, entry_eq);

    if (node == NULL){
        return 0;
    }
    else {
        struct Entry* entry = container_of(node, struct Entry, node);
        if (entry->heap_idx != (size_t)-1){
            heap_delete(&gd.ttl_heap, entry->heap_idx);
            entry->heap_idx = (size_t)-1;
        }
        dlist_delete(&entry->lru_node);

        size_t mem = sizeof(struct Entry) + bufcap(entry->key) + bufcap(entry->val);
        if (gd.used_memory >= mem) gd.used_memory -= mem;
        else gd.used_memory = 0;

        free_Entry(entry);
        free(entry);
        return 1;
    }
}

int incr_decr_Entry(char* key, int64_t delta, int64_t* out_val){
    struct HMap* hmap = &gd.kv_db;

    uint64_t hcode_key = str_hash(key, strlen(key));
    struct HNode** from = lookup_HMap(hmap, key, hcode_key, entry_eq);

    if (from == NULL || *from == NULL){
        return RES_NX;
    }

    struct Entry* entry = container_of(*from, struct Entry, node);

    // Passive/lazy expiration check
    if (entry->heap_idx != (size_t)-1 && gd.ttl_heap.arr[entry->heap_idx].expiration_time <= get_monotonic_msec()){
        del_Entry(key);
        return RES_NX;
    }

    size_t vsz = bufsize(entry->val);
    if (vsz == 0 || vsz > 32){
        return RES_TY;
    }

    char tmp[64];
    memcpy(tmp, bufstart(entry->val), vsz);
    tmp[vsz] = '\0';

    char* endptr = NULL;
    errno = 0;
    long long val = strtoll(tmp, &endptr, 10);
    if (errno != 0 || endptr == tmp || *endptr != '\0'){
        return RES_TY;
    }

    // Overflow / underflow check
    if ((delta > 0 && val > INT64_MAX - delta) ||
        (delta < 0 && val < INT64_MIN - delta)){
        return RES_ERR;
    }

    val += delta;

    char new_str[64];
    int nlen = snprintf(new_str, sizeof(new_str), "%lld", val);

    size_t old_cap = bufcap(entry->val);
    size_t est_cap = ((size_t)nlen > old_cap) ? (size_t)nlen * 2 : old_cap;
    if (est_cap > old_cap && !evict_if_needed(est_cap - old_cap)){
        return RES_ERR;
    }

    clearbuf(entry->val);
    if (!bufappend(entry->val, new_str, nlen)){
        return RES_ERR;
    }

    size_t new_cap = bufcap(entry->val);
    if (new_cap > old_cap) gd.used_memory += (new_cap - old_cap);
    else if (old_cap > new_cap) gd.used_memory -= (old_cap - new_cap);

    // Touch LRU (preserves any existing TTL!)
    dlist_delete(&entry->lru_node);
    dlist_insert_before(&gd.lru_list, &entry->lru_node);

    *out_val = (int64_t)val;
    return RES_OK;
}

struct SnapshotEntryContext {
    FILE* fp;
    uint64_t now_ms;
    uint64_t written_count;
    uint32_t checksum;
};

static void snapshot_write_cb(struct Entry* entry, void* arg){
    struct SnapshotEntryContext* ctx = arg;
    if (entry->heap_idx != (size_t)-1 && gd.ttl_heap.arr[entry->heap_idx].expiration_time <= ctx->now_ms){
        return;
    }

    uint32_t klen = strlen(bufstart(entry->key));
    uint32_t vlen = bufsize(entry->val);
    uint8_t has_ttl = (entry->heap_idx != (size_t)-1) ? 1 : 0;
    uint64_t remaining_ttl_ms = 0;
    if (has_ttl){
        remaining_ttl_ms = gd.ttl_heap.arr[entry->heap_idx].expiration_time - ctx->now_ms;
    }

    fwrite(&klen, sizeof(uint32_t), 1, ctx->fp);
    fwrite(bufstart(entry->key), 1, klen, ctx->fp);
    fwrite(&vlen, sizeof(uint32_t), 1, ctx->fp);
    fwrite(bufstart(entry->val), 1, vlen, ctx->fp);
    fwrite(&has_ttl, sizeof(uint8_t), 1, ctx->fp);
    fwrite(&remaining_ttl_ms, sizeof(uint64_t), 1, ctx->fp);

    ctx->written_count++;
    ctx->checksum = (ctx->checksum * 33) ^ (klen + vlen + has_ttl);
}

int save_snapshot(const char* path){
    if (!path || path[0] == '\0') path = SNAPSHOT_PATH;

    char tmp_path[1200];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%d", path, (int)getpid());

    FILE* fp = fopen(tmp_path, "wb");
    if (!fp){
        msg("save_snapshot: cannot open temp file");
        return 0;
    }

    char magic[] = SNAPSHOT_MAGIC;
    fwrite(magic, 1, sizeof(magic), fp);

    uint64_t now_ms = get_monotonic_msec();
    fwrite(&now_ms, sizeof(uint64_t), 1, fp);

    long count_pos = ftell(fp);
    uint64_t placeholder_count = 0;
    fwrite(&placeholder_count, sizeof(uint64_t), 1, fp);

    struct SnapshotEntryContext ctx = {
        .fp = fp,
        .now_ms = now_ms,
        .written_count = 0,
        .checksum = 5381
    };

    hmap_for_each(&gd.kv_db, snapshot_write_cb, &ctx);

    fseek(fp, count_pos, SEEK_SET);
    fwrite(&ctx.written_count, sizeof(uint64_t), 1, fp);
    fseek(fp, 0, SEEK_END);

    char eof_magic[] = SNAPSHOT_EOF_MAGIC;
    fwrite(eof_magic, 1, sizeof(eof_magic), fp);
    fwrite(&ctx.checksum, sizeof(uint32_t), 1, fp);

    fflush(fp);
    int fd = fileno(fp);
    if (fd >= 0) fsync(fd);
    fclose(fp);

    if (rename(tmp_path, path) != 0){
        msg("save_snapshot: rename failed");
        unlink(tmp_path);
        return 0;
    }

    note("Snapshot saved successfully");
    return 1;
}

int load_snapshot(const char* path){
    if (!path || path[0] == '\0') path = SNAPSHOT_PATH;

    FILE* fp = fopen(path, "rb");
    if (!fp){
        return 0;
    }

    char magic[sizeof(SNAPSHOT_MAGIC)];
    if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic) || strcmp(magic, SNAPSHOT_MAGIC) != 0){
        msg("load_snapshot: invalid magic header");
        fclose(fp);
        return -1;
    }

    uint64_t saved_now_ms = 0;
    if (fread(&saved_now_ms, sizeof(uint64_t), 1, fp) != 1){
        msg("load_snapshot: truncated header");
        fclose(fp);
        return -1;
    }

    uint64_t total_keys = 0;
    if (fread(&total_keys, sizeof(uint64_t), 1, fp) != 1){
        msg("load_snapshot: truncated header");
        fclose(fp);
        return -1;
    }

    uint32_t checksum = 5381;
    uint64_t restored = 0;

    for (uint64_t i = 0; i < total_keys; ++i){
        uint32_t klen = 0;
        if (fread(&klen, sizeof(uint32_t), 1, fp) != 1 || klen > (uint32_t)MAX_PAYLOAD_SIZE){
            msg("load_snapshot: corrupt klen");
            fclose(fp);
            return -1;
        }

        char* kbuf = malloc(klen + 1);
        if (!kbuf){ fclose(fp); return -1; }
        if (fread(kbuf, 1, klen, fp) != klen){
            free(kbuf);
            msg("load_snapshot: truncated key");
            fclose(fp);
            return -1;
        }
        kbuf[klen] = '\0';

        uint32_t vlen = 0;
        if (fread(&vlen, sizeof(uint32_t), 1, fp) != 1 || vlen > (uint32_t)MAX_PAYLOAD_SIZE * 100){
            free(kbuf);
            msg("load_snapshot: corrupt vlen");
            fclose(fp);
            return -1;
        }

        char* vbuf = malloc(vlen + 1);
        if (!vbuf){ free(kbuf); fclose(fp); return -1; }
        if (fread(vbuf, 1, vlen, fp) != vlen){
            free(kbuf);
            free(vbuf);
            msg("load_snapshot: truncated value");
            fclose(fp);
            return -1;
        }
        vbuf[vlen] = '\0';

        uint8_t has_ttl = 0;
        uint64_t remaining_ttl_ms = 0;
        if (fread(&has_ttl, sizeof(uint8_t), 1, fp) != 1 ||
            fread(&remaining_ttl_ms, sizeof(uint64_t), 1, fp) != 1){
            free(kbuf);
            free(vbuf);
            msg("load_snapshot: truncated ttl");
            fclose(fp);
            return -1;
        }

        if (!has_ttl || remaining_ttl_ms > 0){
            set_Entry(kbuf, vbuf);
            if (has_ttl && remaining_ttl_ms > 0){
                set_ttl_Entry_ms(kbuf, (int64_t)remaining_ttl_ms);
            }
            restored++;
        }

        checksum = (checksum * 33) ^ (klen + vlen + has_ttl);
        free(kbuf);
        free(vbuf);
    }

    char eof_magic[sizeof(SNAPSHOT_EOF_MAGIC)];
    uint32_t file_checksum = 0;
    if (fread(eof_magic, 1, sizeof(eof_magic), fp) != sizeof(eof_magic) ||
        strcmp(eof_magic, SNAPSHOT_EOF_MAGIC) != 0 ||
        fread(&file_checksum, sizeof(uint32_t), 1, fp) != 1 ||
        file_checksum != checksum){
        msg("load_snapshot: corrupt or incomplete snapshot footer/checksum");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    char note_buf[128];
    snprintf(note_buf, sizeof(note_buf), "Snapshot restored: %llu keys", (unsigned long long)restored);
    note(note_buf);
    return 1;
}