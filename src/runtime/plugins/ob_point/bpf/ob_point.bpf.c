#include <linux/bpf.h>
#include <asm/ptrace.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* libbpf USDT argument helper — static inline from usdt.bpf.h */
#include <bpf/usdt.bpf.h>

struct alloc_info {
    __u64 size;
    __u64 timestamp_ns;
    int stack_id;
};

struct agg_key {
    __u32 pid;
    __u32 tid;
    int stack_id;
};

struct agg_val {
    __u64 current_bytes;
    __u64 total_alloc;
    __u64 alloc_count;
    __u64 free_count;
    __u64 peak;
    __u64 max_size;
    __u64 min_size;
    __u64 first_seen_ns;
    __u64 last_seen_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 65536);
    __type(key, __u64);
    __type(value, struct alloc_info);
} allocs SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u64);
    __type(value, __u64);
} entry_sz SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16384);
    __type(key, struct agg_key);
    __type(value, struct agg_val);
} agg SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_STACK_TRACE);
    __uint(max_entries, 16384);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, 64 * sizeof(__u64));
} stacks SEC(".maps");

static __always_inline void account_alloc(struct pt_regs *ctx, __u64 size, __u64 pid_tgid) {
    struct agg_key ak = {};
    ak.pid = pid_tgid >> 32;
    ak.tid = (__u32)pid_tgid;

    int stack_id = bpf_get_stackid(ctx, &stacks, BPF_F_USER_STACK);
    if (stack_id < 0) {
        stack_id = 0;
    }
    ak.stack_id = stack_id;

    struct agg_val zero = {};
    struct agg_val *existing = bpf_map_lookup_elem(&agg, &ak);
    if (existing) {
        __sync_fetch_and_add(&existing->current_bytes, size);
        __sync_fetch_and_add(&existing->total_alloc, size);
        __sync_fetch_and_add(&existing->alloc_count, 1);
        __u64 cur = existing->current_bytes;
        if (cur > existing->peak) {
            existing->peak = cur;
        }
        if (size > existing->max_size) existing->max_size = size;
        if (size < existing->min_size) existing->min_size = size;
        existing->last_seen_ns = bpf_ktime_get_ns();
    } else {
        zero.current_bytes = size;
        zero.total_alloc = size;
        zero.alloc_count = 1;
        zero.peak = size;
        zero.max_size = size;
        zero.min_size = size;
        zero.first_seen_ns = bpf_ktime_get_ns();
        zero.last_seen_ns = zero.first_seen_ns;
        bpf_map_update_elem(&agg, &ak, &zero, BPF_ANY);
    }
}

static __always_inline void account_free(__u64 size, __u64 pid_tgid, int alloc_stack_id) {
    if (alloc_stack_id < 0) {
        alloc_stack_id = 0;
    }

    struct agg_key ak = {};
    ak.pid = pid_tgid >> 32;
    ak.tid = (__u32)pid_tgid;
    ak.stack_id = alloc_stack_id;

    struct agg_val *existing = bpf_map_lookup_elem(&agg, &ak);
    if (existing) {
        __sync_fetch_and_sub(&existing->current_bytes, size);
        __sync_fetch_and_add(&existing->free_count, 1);
    }
}

SEC("uprobe")
int on_malloc_enter(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u64 size = PT_REGS_PARM1_CORE(ctx);
    bpf_map_update_elem(&entry_sz, &pid_tgid, &size, BPF_ANY);
    return 0;
}

SEC("uretprobe")
int on_alloc_exit(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u64 *size_ptr = bpf_map_lookup_elem(&entry_sz, &pid_tgid);
    if (!size_ptr) {
        return 0;
    }
    __u64 size = *size_ptr;
    bpf_map_delete_elem(&entry_sz, &pid_tgid);

    void *ptr = (void *)PT_REGS_RC_CORE(ctx);
    if (!ptr) {
        return 0;
    }

    struct alloc_info info = {};
    info.size = size;
    info.timestamp_ns = bpf_ktime_get_ns();
    info.stack_id = bpf_get_stackid(ctx, &stacks, BPF_F_USER_STACK);

    __u64 key = (__u64)ptr;
    bpf_map_update_elem(&allocs, &key, &info, BPF_ANY);

    account_alloc(ctx, size, pid_tgid);
    return 0;
}

SEC("uprobe")
int on_calloc_enter(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u64 nmemb = PT_REGS_PARM1_CORE(ctx);
    __u64 size = PT_REGS_PARM2_CORE(ctx);
    __u64 total = nmemb * size;
    bpf_map_update_elem(&entry_sz, &pid_tgid, &total, BPF_ANY);
    return 0;
}

SEC("uprobe")
int on_realloc_enter(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u64 size = PT_REGS_PARM2_CORE(ctx);
    bpf_map_update_elem(&entry_sz, &pid_tgid, &size, BPF_ANY);
    return 0;
}

SEC("uprobe")
int on_free_enter(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    void *ptr = (void *)PT_REGS_PARM1_CORE(ctx);
    if (!ptr) {
        return 0;
    }

    __u64 key = (__u64)ptr;
    struct alloc_info *info = bpf_map_lookup_elem(&allocs, &key);
    if (!info) {
        return 0;
    }

    __u64 size = info->size;
    int alloc_stack_id = info->stack_id;
    bpf_map_delete_elem(&allocs, &key);

    account_free(size, pid_tgid, alloc_stack_id);
    return 0;
}

char _license[] SEC("license") = "GPL";

/* ===== MemPoint USDT maps and handlers ===== */

struct mp_stat_key {
    __u32 pid;
    __u32 id;   /* MemPointKey enum value */
};

struct mp_stat_val {
    __u64 current_bytes;
    __u64 total_alloc;
    __u64 alloc_count;
    __u64 free_count;
    __u64 peak;
    __u64 max_size;
    __u64 min_size;
    __u64 first_seen_ns;
    __u64 last_seen_ns;
};

struct mp_obj_key {
    __u64 obj_addr;
};

struct mp_obj_val {
    __u32 id;
    __u32 pid;
    __u64 size;
    __u64 enter_ns;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, struct mp_stat_key);
    __type(value, struct mp_stat_val);
} mp_stat_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, struct mp_obj_key);
    __type(value, struct mp_obj_val);
} mp_live_objs SEC(".maps");

SEC("usdt")
int handle_mempoint_enter(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 pid = pid_tgid >> 32;

    long id_ = 0, size = 0, obj = 0;
    bpf_usdt_arg(ctx, 0, &id_);
    bpf_usdt_arg(ctx, 1, &size);
    bpf_usdt_arg(ctx, 2, &obj);

    __u32 id = (__u32)id_;

    /* Key-level aggregation */
    struct mp_stat_key skey = { .pid = pid, .id = id };
    struct mp_stat_val *sval = bpf_map_lookup_elem(&mp_stat_map, &skey);
    if (sval) {
        __sync_fetch_and_add(&sval->current_bytes, size);
        __sync_fetch_and_add(&sval->total_alloc, size);
        __sync_fetch_and_add(&sval->alloc_count, 1);
        __u64 cur = sval->current_bytes;
        if (cur > sval->peak) sval->peak = cur;
        if (size > sval->max_size) sval->max_size = size;
        if (size < sval->min_size) sval->min_size = size;
        sval->last_seen_ns = bpf_ktime_get_ns();
    } else {
        struct mp_stat_val zero = {};
        zero.current_bytes = size;
        zero.total_alloc = size;
        zero.alloc_count = 1;
        zero.peak = size;
        zero.max_size = size;
        zero.min_size = size;
        zero.first_seen_ns = bpf_ktime_get_ns();
        zero.last_seen_ns = zero.first_seen_ns;
        bpf_map_update_elem(&mp_stat_map, &skey, &zero, BPF_ANY);
    }

    /* Object tracking */
    struct mp_obj_key okey = { .obj_addr = obj };
    struct mp_obj_val oval = {
        .id = id,
        .pid = pid,
        .size = size,
        .enter_ns = bpf_ktime_get_ns(),
    };
    bpf_map_update_elem(&mp_live_objs, &okey, &oval, BPF_ANY);

    return 0;
}

SEC("usdt")
int handle_mempoint_leave(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 pid = pid_tgid >> 32;

    long id_ = 0, size = 0, obj = 0;
    bpf_usdt_arg(ctx, 0, &id_);
    bpf_usdt_arg(ctx, 1, &size);
    bpf_usdt_arg(ctx, 2, &obj);

    __u32 id = (__u32)id_;

    /* Key-level aggregation */
    struct mp_stat_key skey = { .pid = pid, .id = id };
    struct mp_stat_val *sval = bpf_map_lookup_elem(&mp_stat_map, &skey);
    if (sval) {
        __sync_fetch_and_sub(&sval->current_bytes, size);
        __sync_fetch_and_add(&sval->free_count, 1);
        sval->last_seen_ns = bpf_ktime_get_ns();
    }

    /* Remove from live objects */
    struct mp_obj_key okey = { .obj_addr = obj };
    bpf_map_delete_elem(&mp_live_objs, &okey);

    return 0;
}

SEC("usdt")
int handle_mempoint_resize(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 pid = pid_tgid >> 32;

    long id_ = 0, delta_u = 0, obj = 0;
    bpf_usdt_arg(ctx, 0, &id_);
    bpf_usdt_arg(ctx, 1, &delta_u);
    bpf_usdt_arg(ctx, 2, &obj);

    __u32 id = (__u32)id_;
    __s64 delta = (__s64)delta_u;

    /* Key-level aggregation */
    struct mp_stat_key skey = { .pid = pid, .id = id };
    struct mp_stat_val *sval = bpf_map_lookup_elem(&mp_stat_map, &skey);
    if (sval) {
        if (delta > 0) {
            __sync_fetch_and_add(&sval->current_bytes, (__u64)delta);
        } else if (delta < 0) {
            __sync_fetch_and_sub(&sval->current_bytes, (__u64)(-delta));
        }
        __u64 cur = sval->current_bytes;
        if (cur > sval->peak) sval->peak = cur;
        sval->last_seen_ns = bpf_ktime_get_ns();
    }

    /* Update live object size */
    struct mp_obj_key okey = { .obj_addr = obj };
    struct mp_obj_val *oval = bpf_map_lookup_elem(&mp_live_objs, &okey);
    if (oval) {
        if (delta > 0) {
            oval->size += (__u64)delta;
        } else if (delta < 0 && oval->size >= (__u64)(-delta)) {
            oval->size -= (__u64)(-delta);
        }
        if (sval) {
            if (oval->size > sval->max_size) sval->max_size = oval->size;
            if (oval->size < sval->min_size) sval->min_size = oval->size;
            if (delta > 0)
                __sync_fetch_and_add(&sval->total_alloc, (__u64)delta);
        }
    }

    return 0;
}

/* ===== ObjectMemPoint snapshot maps and handler ===== */

/* ObjectMemPoint binary layout — must match object_mem_point.h exactly. */
struct mp_struct_key {
    __u32 pid;
    __u32 id;
};

struct mp_field_entry {
    char     name[16];
    __u32    static_size;
    __u32    heap_size;
    __u8     type;       /* 0=plain, 1=container, 2=string */
    __u8     _pad[3];
};

struct mp_struct_val {
    char     type_name[32];
    __u16    num_fields;
    __u16    _pad;
    __u32    total_static;
    __u32    total_heap;
    __u32    padding_bytes;
    struct mp_field_entry fields[16];
};

/* Per-CPU scratch buffer for reading large userspace buffers without blowing
   the 512-byte BPF stack limit. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct mp_struct_val);
} mp_struct_scratch SEC(".maps");

/* Latest struct snapshot per {pid, MemPointKey}. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, struct mp_struct_key);
    __type(value, struct mp_struct_val);
} mp_struct_map SEC(".maps");

SEC("usdt")
int handle_mempoint_struct_snapshot(struct pt_regs *ctx) {
    __u64 pid_tgid = bpf_get_current_pid_tgid();
    __u32 pid = pid_tgid >> 32;

    long id_ = 0, buf_ptr = 0, buf_len = 0;
    bpf_usdt_arg(ctx, 0, &id_);
    bpf_usdt_arg(ctx, 1, &buf_ptr);
    bpf_usdt_arg(ctx, 2, &buf_len);

    __u32 id = (__u32)id_;

    struct mp_struct_key skey = {};
    skey.pid = pid;
    skey.id = id;

    /* Use per-CPU scratch buffer to avoid large stack allocation. */
    __u32 zero = 0;
    struct mp_struct_val *scratch = bpf_map_lookup_elem(&mp_struct_scratch, &zero);
    if (!scratch) return 0;

    /* Read the entire SnapshotBuffer from userspace into scratch space. */
    __u64 len = (__u64)buf_len;
    if (len < sizeof(struct mp_struct_val)) len = sizeof(struct mp_struct_val);
    if (len > sizeof(struct mp_struct_val)) len = sizeof(struct mp_struct_val);

    __builtin_memset(scratch, 0, sizeof(*scratch));
    long ret = bpf_probe_read_user(scratch, len, (void *)buf_ptr);
    if (ret < 0) return 0;

    /* Store in persistent map (replaces any previous snapshot for this key). */
    bpf_map_update_elem(&mp_struct_map, &skey, scratch, BPF_ANY);

    return 0;
}
