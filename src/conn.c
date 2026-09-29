#include "conn.h"
#include "config.h"
#include <limits.h>

#define container_of(ptr, T, member) \
    ((T *)( (char *)ptr - offsetof(T, member) ))

struct Conn* create_conn(int fd){
    struct Conn* conn = NULL;
    if (gd.pooled_conn_count == 0){
        conn = (struct Conn*)malloc(sizeof(struct Conn));
        if (conn == NULL){
            msg("create_conn (conn.c): malloc");
            return NULL;
        }
        
        conn->rbuf = newbuf(INITIAL_BUFFER_CAP);
        if (conn->rbuf == NULL){
            free(conn);
            return NULL;
        }
        conn->wbuf = newbuf(INITIAL_BUFFER_CAP);
        if (conn->wbuf == NULL){
            freebuf(conn->rbuf);
            free(conn);
            return NULL;
        }
    }
    else {
        conn = (struct Conn*)container_of(gd.conn_pool.next, struct Conn, idle_node);
        dlist_delete(gd.conn_pool.next);
        gd.pooled_conn_count--;
    }

    conn->fd = fd;
    conn->want_close = 0;
    conn->authenticated = (requirepass[0] == '\0') ? 1 : 0;
    conn->last_used_time = get_monotonic_msec();
    dlist_insert_before(&gd.idle_list, &conn->idle_node);
    gd.connected_clients++;
    gd.stat_total_connections++;

    return conn;
}

void free_conn(struct Conn* conn){
    dlist_delete(&conn->idle_node);
    if (gd.connected_clients > 0) gd.connected_clients--;

    if (gd.pooled_conn_count == MAX_POOLED_CONN_COUNT){
        freebuf(conn->rbuf);
        freebuf(conn->wbuf);
        free(conn);
    }
    else {
        dlist_insert_before(&gd.conn_pool, &conn->idle_node);
        clearbuf(conn->rbuf);
        clearbuf(conn->wbuf);
        conn->authenticated = (requirepass[0] == '\0') ? 1 : 0;
        gd.pooled_conn_count++;
    }
}

void hard_free_conn(struct Conn* conn){
    dlist_delete(&conn->idle_node);
    if (gd.connected_clients > 0) gd.connected_clients--;
    freebuf(conn->rbuf);
    freebuf(conn->wbuf);
    free(conn);
}

int smallest_remaining_time(){
    int timeout = -1;
    uint64_t time_now = get_monotonic_msec();

    if (IDLE_TIMEOUT_MS > 0 && !dlist_empty(&gd.idle_list)){
        uint64_t last_used_time = ((struct Conn*)(container_of(gd.idle_list.next, struct Conn, idle_node)))->last_used_time;
        if (time_now >= last_used_time + (uint64_t)IDLE_TIMEOUT_MS){
            timeout = 0;
        } else {
            uint64_t diff = (last_used_time + (uint64_t)IDLE_TIMEOUT_MS) - time_now;
            timeout = (diff > INT_MAX) ? INT_MAX : (int)diff;
        }
    }

    if (gd.ttl_heap.sz > 0){
        if (gd.ttl_heap.arr[0].expiration_time <= time_now){
            timeout = 0;
        } else {
            uint64_t diff = gd.ttl_heap.arr[0].expiration_time - time_now;
            int ttl_timeout = (diff > INT_MAX) ? INT_MAX : (int)diff;
            if (timeout < 0 || ttl_timeout < timeout){
                timeout = ttl_timeout;
            }
        }
    }

    return timeout;
}