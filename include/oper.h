#ifndef OPER_H
#define OPER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "response.h"
#include "g_data.h"
#include "timer.h"
#include "config.h"

extern struct g_data gd;

struct buf* get_Entry(char* key);
int exists_Entry(char* key);
int set_Entry(char* key, char* val);
int del_Entry(char* key);
int set_ttl_Entry_ms(char* key, int64_t ttl);
int set_ttl_Entry_sec(char* key, int64_t ttl);
int64_t get_ttl_Entry_ms(char* key);
int64_t get_ttl_Entry_sec(char* key);

int incr_decr_Entry(char* key, int64_t delta, int64_t* out_val);
int glob_match(const char* pattern, const char* str);
int evict_if_needed(size_t needed);

int save_snapshot(const char* path);
int load_snapshot(const char* path);

#endif