/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifdef SPDIAG_ENABLE_MEMPOINT


#include "point_mem_profiler.h"
#include "spdiag/mem_point_keys.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <unordered_map>

// BPF map data structures — must match ob_point.bpf.c
struct mp_stat_key {
    uint32_t pid;
    uint32_t id;
};

struct mp_stat_val {
    uint64_t current_bytes;
    uint64_t total_alloc;
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t peak;
    uint64_t max_size;
    uint64_t min_size;
    uint64_t first_seen_ns;
    uint64_t last_seen_ns;
};

struct mp_obj_key {
    uint64_t obj_addr;
};

struct mp_obj_val {
    uint32_t id;
    uint32_t pid;
    uint64_t size;
    uint64_t enter_ns;
};

// Must match ob_point.bpf.c struct snapshot definitions
struct mp_struct_key {
    uint32_t pid;
    uint32_t id;
};

struct mp_field_entry {
    char     name[16];
    uint32_t static_size;
    uint32_t heap_size;
    uint8_t  type;
    uint8_t  _pad[3];
};

struct mp_struct_val {
    char     type_name[32];
    uint16_t num_fields;
    uint16_t _pad;
    uint32_t total_static;
    uint32_t total_heap;
    uint32_t padding_bytes;
    mp_field_entry fields[16];
};

namespace SpDiag {

PointMemProfiler::PointMemProfiler() = default;

PointMemProfiler::~PointMemProfiler() {
    Detach();
}

const char* PointMemProfiler::KeyName(uint32_t id) {
    if (id == 0 || id >= static_cast<uint32_t>(MemPointKey::MEM_POINT_KEY_COUNT)) {
        return "unknown";
    }
    return memPointKeyDescriptions[id - 1];
}

bool PointMemProfiler::Attach(struct bpf_object* obj, pid_t pid,
                               const std::string& binaryPath) {
    if (attached_) return false;
    if (!obj) return false;

    // Find USDT programs
    static const char* progNames[] = {
        "handle_mempoint_enter",
        "handle_mempoint_leave",
        "handle_mempoint_resize",
        "handle_mempoint_struct_snapshot",
    };

    struct bpf_link** links[] = { &enterLink_, &leaveLink_, &resizeLink_, &structSnapshotLink_ };
    int attachedCount = 0;

    for (int i = 0; i < 4; ++i) {
        struct bpf_program* prog = bpf_object__find_program_by_name(obj, progNames[i]);
        if (!prog) {
            std::cerr << "mempoint: BPF program not found: " << progNames[i] << std::endl;
            Detach();
            return false;
        }

        bpf_usdt_opts usdt_opts{};
        usdt_opts.sz = sizeof(usdt_opts);
        struct bpf_link* link = bpf_program__attach_usdt(
            prog, pid, binaryPath.c_str(), "ob_mempoint",
            progNames[i] + strlen("handle_mempoint_"),  // "enter", "leave", "resize"
            &usdt_opts);

        if (!link) {
            std::cerr << "mempoint: USDT attach failed for " << progNames[i]
                      << ": " << std::strerror(errno) << std::endl;
            continue;
        }
        *(links[i]) = link;
        ++attachedCount;
    }

    if (attachedCount == 0) {
        std::cerr << "mempoint: no USDT probes were attached" << std::endl;
        Detach();
        return false;
    }

    // Get map FDs
    struct bpf_map* statMap = bpf_object__find_map_by_name(obj, "mp_stat_map");
    struct bpf_map* liveMap = bpf_object__find_map_by_name(obj, "mp_live_objs");

    if (!statMap || !liveMap) {
        std::cerr << "mempoint: BPF maps not found" << std::endl;
        Detach();
        return false;
    }

    statMapFd_ = bpf_map__fd(statMap);
    liveObjsFd_ = bpf_map__fd(liveMap);

    struct bpf_map* structMap = bpf_object__find_map_by_name(obj, "mp_struct_map");
    if (structMap) {
        structMapFd_ = bpf_map__fd(structMap);
    }

    targetPid_ = pid;
    attached_ = true;
    return true;
}

bool PointMemProfiler::Collect(DataSink* sink) {
    if (!attached_ || !sink) return false;

    CollectStatMap(sink);
    CollectLeakInfo(sink);
    CollectStructMap(sink);
    return true;
}

void PointMemProfiler::CollectStatMap(DataSink* sink) {
    mp_stat_key key = {}, nextKey = {};

    while (bpf_map_get_next_key(statMapFd_, &key, &nextKey) == 0) {
        mp_stat_val val = {};
        if (bpf_map_lookup_elem(statMapFd_, &nextKey, &val) == 0) {
            std::string prefix = "mempoint." + std::to_string(nextKey.pid) +
                                 "." + std::to_string(nextKey.id);

            std::vector<std::pair<std::string, std::string>> items = {
                {prefix + ".current_bytes", std::to_string(val.current_bytes)},
                {prefix + ".total_alloc",   std::to_string(val.total_alloc)},
                {prefix + ".alloc_count",   std::to_string(val.alloc_count)},
                {prefix + ".free_count",    std::to_string(val.free_count)},
                {prefix + ".peak",          std::to_string(val.peak)},
                {prefix + ".max_size",      std::to_string(val.max_size)},
                {prefix + ".min_size",      std::to_string(val.min_size)},
                {prefix + ".first_seen_ns", std::to_string(val.first_seen_ns)},
                {prefix + ".key_name",      KeyName(nextKey.id)},
            };

            sink->SubmitBatch(items);
        }
        key = nextKey;
    }
}

void PointMemProfiler::CollectLeakInfo(DataSink* sink) {
    static constexpr uint64_t LEAK_THRESHOLD_NS = 300000000000ULL;  // 5 minutes

    mp_obj_key key = {}, nextKey = {};
    uint64_t nowNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    // Count leak suspects per key
    std::unordered_map<uint32_t, uint64_t> leakCounts;
    std::unordered_map<uint32_t, uint64_t> leakMaxAge;

    while (bpf_map_get_next_key(liveObjsFd_, &key, &nextKey) == 0) {
        mp_obj_val val = {};
        if (bpf_map_lookup_elem(liveObjsFd_, &nextKey, &val) == 0) {
            uint64_t ageNs = (val.enter_ns < nowNs) ? (nowNs - val.enter_ns) : 0;
            if (ageNs > LEAK_THRESHOLD_NS) {
                leakCounts[val.id]++;
                if (ageNs > leakMaxAge[val.id]) {
                    leakMaxAge[val.id] = ageNs;
                }
            }
        }
        key = nextKey;
    }

    for (auto& [id, count] : leakCounts) {
        std::string prefix = "mempoint." + std::to_string(targetPid_) +
                             ".leak." + std::to_string(id);
        sink->SubmitBatch({
            {prefix + ".count",      std::to_string(count)},
            {prefix + ".max_age_ns", std::to_string(leakMaxAge[id])},
            {prefix + ".key_name",   KeyName(id)},
        });
    }
}

void PointMemProfiler::CollectStructMap(DataSink* sink) {
    if (structMapFd_ < 0) return;

    mp_struct_key key = {}, nextKey = {};

    while (bpf_map_get_next_key(structMapFd_, &key, &nextKey) == 0) {
        mp_struct_val val = {};
        if (bpf_map_lookup_elem(structMapFd_, &nextKey, &val) == 0) {
            std::string prefix = "mempoint." + std::to_string(nextKey.pid) +
                                 "." + std::to_string(nextKey.id) + ".struct";

            std::vector<std::pair<std::string, std::string>> items = {
                {prefix + ".type_name",     std::string(val.type_name)},
                {prefix + ".total_static",  std::to_string(val.total_static)},
                {prefix + ".total_heap",    std::to_string(val.total_heap)},
                {prefix + ".padding_bytes", std::to_string(val.padding_bytes)},
                {prefix + ".num_fields",    std::to_string(val.num_fields)},
            };

            uint16_t numFields = val.num_fields;
            if (numFields > 16) numFields = 16;

            for (uint16_t i = 0; i < numFields; ++i) {
                const auto& field = val.fields[i];
                if (field.name[0] == '\0') continue;

                std::string fp = prefix + ".field." + std::to_string(i);
                items.emplace_back(fp + ".name",        std::string(field.name));
                items.emplace_back(fp + ".static_size", std::to_string(field.static_size));
                items.emplace_back(fp + ".heap_size",   std::to_string(field.heap_size));
                items.emplace_back(fp + ".type",        std::to_string(field.type));
            }

            sink->SubmitBatch(items);
        }
        key = nextKey;
    }
}

void PointMemProfiler::Detach() {
    if (enterLink_) { bpf_link__destroy(enterLink_); enterLink_ = nullptr; }
    if (leaveLink_) { bpf_link__destroy(leaveLink_); leaveLink_ = nullptr; }
    if (resizeLink_) { bpf_link__destroy(resizeLink_); resizeLink_ = nullptr; }
    if (structSnapshotLink_) { bpf_link__destroy(structSnapshotLink_); structSnapshotLink_ = nullptr; }

    statMapFd_ = -1;
    liveObjsFd_ = -1;
    structMapFd_ = -1;
    targetPid_ = -1;
    attached_ = false;
}

} // namespace SpDiag

#endif // SPDIAG_ENABLE_MEMPOINT
