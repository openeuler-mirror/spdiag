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

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <thread>
#include <limits>
#include <iomanip>
#include <memory>
#include <vector>

#include <unistd.h>
#include <csignal>

#include "spdiag/mem_point.h"
#include "spdiag/mem_point_keys.h"
#include "spdiag/tracked_containers.h"
#include "spdiag/object_mem_point.h"

using namespace SpDiag;

namespace {

volatile std::sig_atomic_t gStartSignal = 0;

void OnSigUsr1(int) {
    gStartSignal = 1;
}

struct Config {
    std::string mode;
    int count = 100;
    int repeat = 1;
    bool wait = false;
    bool loop = false;
    int intervalMs = 500;
    uint64_t iterations = 1000000;
};

void PrintUsage() {
    std::cerr <<
        "Usage: mempoint_demo [options]\n"
        "\n"
        "MemPoint SDK demo for spdiag mempoint.\n"
        "Demonstrates all MemPoint tracking patterns.\n"
        "\n"
        "Modes:\n"
        "  stack_value     TrackValue RAII for local variables\n"
        "  tracked_vector  tracked::vector dynamic growth\n"
        "  tracked_string  tracked::string append/assign\n"
        "  tracked_hashmap tracked::unordered_map insert/erase\n"
        "  raw_buffer      TrackBuffer raw pointer + manual lifecycle\n"
        "  leak            Intentional leak (no destructor)\n"
        "  struct_member   POD struct with external MemPoint guard\n"
        "  heap_object     RAII class with embedded MemPoint member\n"
        "  object_pool     Pre-allocated pool with acquire/release\n"
        "  custom_lifecycle Delayed destruction via unique_ptr queue\n"
        "  vector_growth   Vector doubling reallocation pattern\n"
        "  string_sso      String SSO boundary crossing\n"
        "  map_churn       Map mass insert/erase churn\n"
        "  spike           Brief large allocation spike\n"
        "  nested          Nested vector<string> multi-key\n"
        "  realloc_cascade Vector resize/shrink_to_fit cascade\n"
        "  struct_breakdown ObjectMemPoint per-field memory breakdown demo\n"
        "  benchmark       Measure single TrackValue overhead\n"
        "\n"
        "Options:\n"
        "  --mode <mode>      Run single mode (default: all)\n"
        "  --count <N>        Iteration count (default: 100)\n"
        "  --repeat <N>       Repeat count (default: 1)\n"
        "  --iterations <N>   Benchmark iterations (default: 1000000)\n"
        "  --loop             Run in continuous loop\n"
        "  --interval <ms>    Loop interval (default: 500)\n"
        "  --wait             Wait for ENTER before starting\n"
        "  --help, -h         Show this help\n";
}

Config ParseArgs(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else if (arg == "--mode" && i + 1 < argc) {
            cfg.mode = argv[++i];
        } else if (arg == "--count" && i + 1 < argc) {
            cfg.count = std::atoi(argv[++i]);
        } else if (arg == "--repeat" && i + 1 < argc) {
            cfg.repeat = std::atoi(argv[++i]);
        } else if (arg == "--loop") {
            cfg.loop = true;
        } else if (arg == "--iterations" && i + 1 < argc) {
            cfg.iterations = std::stoull(argv[++i]);
        } else if (arg == "--interval" && i + 1 < argc) {
            cfg.intervalMs = std::atoi(argv[++i]);
        } else if (arg == "--wait") {
            cfg.wait = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            PrintUsage();
            std::exit(1);
        }
    }
    return cfg;
}

volatile size_t gSink = 0;

class TxContext {
public:
    explicit TxContext(uint64_t id)
        : txId_(id)
        , mp_(MemPoint::TrackObject(MemPointKey::TX_EXEC_CTX, *this)) {}

    void AppendUndo(const uint8_t* data, size_t len) {
        undoBuf_.insert(undoBuf_.end(), data, data + len);
        mp_.UpdateSize(sizeof(TxContext) + undoBuf_.capacity());
    }

    uint64_t Id() const { return txId_; }

private:
    uint64_t txId_;
    std::vector<uint8_t> undoBuf_;
    MemPoint mp_;
};

} // namespace

static void DemoStackValue(int count) {
    for (int i = 0; i < count; ++i) {
        uint64_t value = static_cast<uint64_t>(i) * 1024;
        MemPoint::TrackValue(
            MemPointKey::RPC_REQUEST_BUF, &value, sizeof(value));
        gSink += value;
    }
}

static void DemoTrackedVector(int count) {
    tracked::vector<int, MemPointKey::SQL_PLAN_CACHE> vec;
    vec.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        vec.push_back(i);
    }
    gSink += static_cast<size_t>(vec[0]);
    vec.clear();
}

static void DemoTrackedString(int count) {
    tracked::string<MemPointKey::LOG_WRITE_BUFFER> str;
    for (int i = 0; i < count; ++i) {
        str.append("chunk_" + std::to_string(i) + " ");
    }
    gSink += str.size();
    str.assign("reset");
    str.clear();
}

static void DemoTrackedHashmap(int count) {
    tracked::unordered_map<int, int, MemPointKey::SESSION_CONN_CTX> map;
    for (int i = 0; i < count; ++i) {
        map[i] = i * 10;
    }
    gSink += map.size();
    for (int i = 0; i < count / 2; ++i) {
        map.erase(i);
    }
}

static void DemoVectorGrowth(int count) {
    tracked::vector<int, MemPointKey::SQL_VECTOR_GROW> vec;
    for (int i = 0; i < count; ++i) {
        vec.push_back(i);
    }
    gSink += static_cast<size_t>(vec.back());
}

static void DemoStringSSO(int count) {
    tracked::string<MemPointKey::LOG_STRING_SSO> s;
    s.append("hi");                                     // SSO, no heap alloc
    for (int i = 0; i < count; ++i) {
        s.append("chunk_" + std::to_string(i) + " ");
    }
    gSink += s.size();
}

static void DemoMapChurn(int count) {
    tracked::unordered_map<int, int, MemPointKey::CONN_MAP_CHURN> map;
    int insertCount = count;
    int eraseCount = count * 9 / 10;
    int reinsertCount = count / 2;

    for (int i = 0; i < insertCount; ++i) map[i] = i;
    for (int i = 0; i < eraseCount; ++i) map.erase(i);
    for (int i = insertCount; i < insertCount + reinsertCount; ++i) map[i] = i;
    gSink += map.size();
}

static void DemoSpike(int /*count*/) {
    tracked::vector<char, MemPointKey::RPC_SPIKE> big;
    big.resize(10 * 1024 * 1024);   // 10 MiB spike
    gSink += big.size();
    big.clear();
    big.shrink_to_fit();
}

static void DemoNested(int count) {
    tracked::vector<tracked::string<MemPointKey::SQL_NESTED_STR>,
                    MemPointKey::SQL_NESTED_VEC> rows;
    rows.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        rows.push_back(
            tracked::string<MemPointKey::SQL_NESTED_STR>(100, 'x'));
    }
    gSink += rows.size();
}

static void DemoReallocCascade(int /*count*/) {
    tracked::vector<int, MemPointKey::SQL_REALLOC> vec;
    vec.resize(100);        // allocate ~400B
    gSink += vec.size();
    vec.resize(1000);       // allocate ~4000B, deallocate ~400B
    vec.resize(500);        // no realloc (vector doesn't shrink)
    vec.shrink_to_fit();    // allocate ~2000B, deallocate ~4000B
    vec.resize(2000);       // allocate ~8000B, deallocate ~2000B
    vec.clear();
    vec.shrink_to_fit();    // deallocate ~8000B
}

static void DemoRawBuffer(int count) {
    for (int i = 0; i < count; ++i) {
        auto buf = new char[2048];
        std::memset(buf, static_cast<unsigned char>(i & 0xFF), 2048);
        MemPoint::TrackBuffer(
            MemPointKey::RPC_RESPONSE_BUF, buf, 2048);
        gSink += static_cast<size_t>(buf[0]);
        delete[] buf;
    }
}

static void DemoLeak(int count) {
    for (int i = 0; i < count; ++i) {
        auto buf = new char[4096];
        std::memset(buf, 0, 4096);
        // Heap-allocate MemPoint: move transfers active_ to heap object,
        // temporary destructs as no-op, heap guard never destructs → leave
        // probe never fires → allocs > frees → eBPF detects leak
        new MemPoint(MemPoint::TrackBuffer(
            MemPointKey::TX_UNDO_BUFFER, buf, 4096));
        gSink += static_cast<size_t>(buf[0]);
    }
}

static void DemoBenchmark(uint64_t iterations) {
    std::cerr << "  [benchmark] Measuring TrackValue overhead (N=" << iterations << ")" << std::endl;

    // Warm up
    for (int i = 0; i < 100; ++i) {
        MemPoint::TrackValue(
            MemPointKey::RPC_REQUEST_BUF, &i, sizeof(i));
    }

    auto startTotal = std::chrono::steady_clock::now();

    for (uint64_t i = 0; i < iterations; ++i) {
        MemPoint::TrackValue(
            MemPointKey::RPC_REQUEST_BUF, &i, sizeof(i));
    }

    auto endTotal = std::chrono::steady_clock::now();

    uint64_t totalDuration = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            endTotal - startTotal).count());

    double avgPerIteration = static_cast<double>(totalDuration)
                            / static_cast<double>(iterations);

    std::cout << "\n=== Benchmark Results ===\n";
    std::cout << "  Iterations:         " << iterations << "\n";
    std::cout << "  Total Duration:     " << std::fixed << std::setprecision(3)
              << (static_cast<double>(totalDuration) / 1000000.0) << " ms\n";
    std::cout << "  Avg per iteration:  " << std::fixed << std::setprecision(2)
              << avgPerIteration << " ns\n";

    if (avgPerIteration <= 5.0) {
        std::cout << "  [PASS] Disabled-state overhead is within target.\n";
    } else if (avgPerIteration <= 2000.0) {
        std::cout << "  [INFO] Overhead suggests USDT probes are active.\n";
    } else {
        std::cout << "  [WARN] Overhead exceeds 2 us — investigate probe cost.\n";
    }
}

static void DemoStructMember(int count) {
    struct RawEntry {
        uint64_t key;
        uint64_t version;
        uint8_t body[256];
    };

    std::vector<std::pair<RawEntry*, MemPoint*>> batch;
    batch.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        auto* entry = new RawEntry{};
        entry->key = static_cast<uint64_t>(i);
        entry->version = 1;
        std::memset(entry->body, static_cast<unsigned char>(i), sizeof(entry->body));

        auto* guard = new MemPoint(
            MemPoint::TrackObject(MemPointKey::SQL_RESULT_SET, *entry));
        batch.push_back({entry, guard});
        gSink += entry->key;
    }

    for (auto& [entry, guard] : batch) {
        delete guard;
        delete entry;
    }
}

static void DemoHeapObject(int count) {
    std::vector<std::unique_ptr<TxContext>> contexts;
    contexts.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i) {
        auto ctx = std::make_unique<TxContext>(static_cast<uint64_t>(i));
        uint8_t data[128];
        std::memset(data, static_cast<unsigned char>(i), sizeof(data));
        ctx->AppendUndo(data, sizeof(data));
        gSink += ctx->Id();
        contexts.push_back(std::move(ctx));
    }
}

static void DemoObjectPool(int count) {
    struct PooledBuf {
        uint64_t requestId;
        uint8_t data[512];
    };

    constexpr int kPoolSize = 8;
    std::vector<PooledBuf*> pool(kPoolSize);
    std::vector<MemPoint*> guards(kPoolSize, nullptr);

    for (int i = 0; i < kPoolSize; ++i) {
        pool[i] = new PooledBuf{};
    }

    for (int i = 0; i < count; ++i) {
        int slot = i % kPoolSize;

        delete guards[slot];
        guards[slot] = nullptr;

        pool[slot]->requestId = static_cast<uint64_t>(i);
        std::memset(pool[slot]->data, static_cast<unsigned char>(i),
                    sizeof(pool[slot]->data));
        guards[slot] = new MemPoint(MemPoint::TrackObject(
            MemPointKey::NET_SEND_BUF, *pool[slot]));

        gSink += pool[slot]->requestId;
    }

    for (int i = 0; i < kPoolSize; ++i) {
        delete guards[i];
        delete pool[i];
    }
}

static void DemoCustomLifecycle(int count) {
    struct RequestContext {
        uint64_t reqId;
        std::vector<uint8_t> body;
    };

    struct ReqGuard {
        RequestContext* ctx;
        MemPoint* mp;
        ~ReqGuard() { delete mp; delete ctx; }
    };

    std::vector<std::unique_ptr<ReqGuard>> inflight;
    constexpr size_t kMaxInflight = 10;

    for (int i = 0; i < count; ++i) {
        auto guard = std::make_unique<ReqGuard>();
        guard->ctx = new RequestContext{};
        guard->ctx->reqId = static_cast<uint64_t>(i);
        guard->ctx->body.resize(128 + static_cast<size_t>(i % 5) * 64);
        std::memset(guard->ctx->body.data(), static_cast<unsigned char>(i),
                    guard->ctx->body.size());

        guard->mp = new MemPoint(MemPoint::TrackObject(
            MemPointKey::ASYNC_TASK_CTX, *guard->ctx));

        gSink += guard->ctx->reqId;
        inflight.push_back(std::move(guard));

        while (inflight.size() > kMaxInflight) {
            inflight.erase(inflight.begin());
        }
    }
}

static void DemoStructBreakdown(int count) {
    // Scenario 1: Plain struct with only POD fields
    struct PlainData {
        int32_t id;
        double score;
        uint8_t flags;
        bool active;
    };

    PlainData plain{};
    plain.id = 42;
    plain.score = 3.14;
    plain.flags = 0xFF;
    plain.active = true;
    {
        auto mp = ObjectMemPoint<PlainData>("PlainData", MemPointKey::RPC_REQUEST_BUF, plain);
        mp.Field("id", &PlainData::id)
          .Field("score", &PlainData::score)
          .Field("flags", &PlainData::flags)
          .Field("active", &PlainData::active);
        mp.Snapshot();
    }

    // Scenario 2: Struct with containers
    struct ContainerData {
        int32_t id;
        std::vector<int> items;
        std::string label;
        std::map<int, int> scores;
    };

    ContainerData container{};
    container.id = 100;
    container.items.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) container.items.push_back(i);
    container.label = "struct_breakdown_demo_with_a_long_name_to_exceed_sso";
    container.scores[1] = 90;
    container.scores[2] = 85;
    {
        auto mp = ObjectMemPoint<ContainerData>("ContainerData", MemPointKey::SQL_PLAN_CACHE, container);
        mp.Field("id", &ContainerData::id)
          .ContainerField("items", &ContainerData::items)
          .ContainerField("label", &ContainerData::label)
          .ContainerField("scores", &ContainerData::scores);
        mp.Snapshot();
    }

    // Scenario 3: Class with private members
    // Field registration must happen inside the class because forming
    // &T::private_member requires access at the call site — friend on
    // ObjectMemPoint alone doesn't help the caller form the pointer.
    class SecretPayload {
        uint64_t secretId_;
        std::vector<uint8_t> payload_;
        std::string name_;
    public:
        SecretPayload(uint64_t id, const std::string& name)
            : secretId_(id), name_(name) {}
        void Append(const uint8_t* data, size_t len) {
            payload_.insert(payload_.end(), data, data + len);
        }
        void TakeSnapshot() {
            auto mp = ObjectMemPoint<SecretPayload>(
                "SecretPayload", MemPointKey::TX_EXEC_CTX, *this);
            mp.Field("secretId_", &SecretPayload::secretId_)
              .ContainerField("payload_", &SecretPayload::payload_)
              .ContainerField("name_", &SecretPayload::name_);
            mp.Snapshot();
        }
    };

    SecretPayload secret(999, "classified");
    uint8_t buf[256];
    std::memset(buf, 0xAB, sizeof(buf));
    for (int i = 0; i < count / 10; ++i) secret.Append(buf, sizeof(buf));
    secret.TakeSnapshot();

    // Scenario 4: Nested containers (vector of strings)
    struct NestedData {
        int32_t count;
        std::vector<std::string> rows;
        std::unordered_map<int, std::string> cache;
    };

    NestedData nested{};
    nested.count = count;
    for (int i = 0; i < count / 2; ++i) {
        nested.rows.push_back("row_" + std::to_string(i) + "_data_padding");
        nested.cache[i] = "cached_" + std::to_string(i);
    }
    {
        auto mp = ObjectMemPoint<NestedData>("NestedData", MemPointKey::SESSION_CONN_CTX, nested);
        mp.Field("count", &NestedData::count)
          .ContainerField("rows", &NestedData::rows)
          .ContainerField("cache", &NestedData::cache);
        mp.Snapshot();
    }

    // Scenario 5: SSO string (short string, no heap)
    struct SSOData {
        int32_t code;
        std::string shortText;  // "hello" fits in SSO
        std::vector<double> empty;
    };

    SSOData sso{};
    sso.code = 200;
    sso.shortText = "hello";
    {
        auto mp = ObjectMemPoint<SSOData>("SSOData", MemPointKey::LOG_WRITE_BUFFER, sso);
        mp.Field("code", &SSOData::code)
          .ContainerField("shortText", &SSOData::shortText)
          .ContainerField("empty", &SSOData::empty);
        mp.Snapshot();
    }

    // Scenario 6: Large struct with many fields (near 16-field limit)
    struct WideData {
        int f1, f2, f3, f4, f5, f6, f7, f8;
        std::string s1, s2;
        std::vector<int> v1;
    };

    WideData wide{};
    wide.f1 = 1; wide.f2 = 2; wide.f3 = 3; wide.f4 = 4;
    wide.f5 = 5; wide.f6 = 6; wide.f7 = 7; wide.f8 = 8;
    wide.s1 = "field_s1_extended_to_force_heap_alloc";
    wide.v1.resize(100);
    {
        auto mp = ObjectMemPoint<WideData>("WideData", MemPointKey::SQL_RESULT_SET, wide);
        mp.Field("f1", &WideData::f1).Field("f2", &WideData::f2)
          .Field("f3", &WideData::f3).Field("f4", &WideData::f4)
          .Field("f5", &WideData::f5).Field("f6", &WideData::f6)
          .Field("f7", &WideData::f7).Field("f8", &WideData::f8)
          .ContainerField("s1", &WideData::s1)
          .ContainerField("s2", &WideData::s2)
          .ContainerField("v1", &WideData::v1);
        mp.Snapshot();
    }

    std::cerr << "  [struct_breakdown] All 6 scenarios completed" << std::endl;
}

int main(int argc, char* argv[]) {
    Config cfg = ParseArgs(argc, argv);

    if (!cfg.mode.empty() && cfg.mode != "stack_value"
        && cfg.mode != "tracked_vector" && cfg.mode != "tracked_string"
        && cfg.mode != "tracked_hashmap" && cfg.mode != "raw_buffer"
        && cfg.mode != "leak" && cfg.mode != "struct_member"
        && cfg.mode != "heap_object" && cfg.mode != "object_pool"
        && cfg.mode != "custom_lifecycle"
        && cfg.mode != "vector_growth" && cfg.mode != "string_sso"
        && cfg.mode != "map_churn" && cfg.mode != "spike"
        && cfg.mode != "nested" && cfg.mode != "realloc_cascade"
        && cfg.mode != "struct_breakdown"
        && cfg.mode != "benchmark") {
        std::cerr << "Unknown mode: " << cfg.mode << "\n";
        return 1;
    }

    std::cerr << "mempoint_demo  PID=" << getpid() << std::endl;
    std::cerr << "  mode=" << (cfg.mode.empty() ? "all" : cfg.mode)
              << "  count=" << cfg.count
              << "  repeat=" << cfg.repeat << std::endl;

    if (cfg.wait) {
        std::signal(SIGUSR1, OnSigUsr1);
        std::cerr << "  Press ENTER or send SIGUSR1 to start..." << std::endl;

        while (gStartSignal == 0) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            struct timeval tv = {0, 100000};
            if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0) {
                char tmp;
                while (read(STDIN_FILENO, &tmp, 1) == 1 && tmp != '\n') {
                }
                break;
            }
        }
    }

    if (!cfg.mode.empty() && cfg.mode != "leak" && !cfg.loop) {
        std::cerr << "\n  Start mempoint NOW in another terminal:\n";
        std::cerr << "    sudo spdiag mempoint --pid " << getpid() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    bool ranLeak = false;
    auto runOnce = [&]() {
        if (cfg.mode == "benchmark") {
            DemoBenchmark(cfg.iterations);
            return;
        }
        if (cfg.mode.empty() || cfg.mode == "stack_value") {
            std::cerr << "  [stack_value] " << cfg.count << "x TrackValue RAII" << std::endl;
            DemoStackValue(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "tracked_vector") {
            std::cerr << "  [tracked_vector] " << cfg.count << "x push + clear" << std::endl;
            DemoTrackedVector(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "tracked_string") {
            std::cerr << "  [tracked_string] " << cfg.count << "x append" << std::endl;
            DemoTrackedString(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "tracked_hashmap") {
            std::cerr << "  [tracked_hashmap] " << cfg.count << "x insert/erase" << std::endl;
            DemoTrackedHashmap(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "raw_buffer") {
            std::cerr << "  [raw_buffer] " << cfg.count << "x 2KB alloc+free" << std::endl;
            DemoRawBuffer(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "leak") {
            std::cerr << "  [leak] " << cfg.count << "x 4KB leaked" << std::endl;
            DemoLeak(cfg.count);
            ranLeak = true;
        }
        if (cfg.mode.empty() || cfg.mode == "struct_member") {
            std::cerr << "  [struct_member] " << cfg.count << "x POD + external guard" << std::endl;
            DemoStructMember(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "heap_object") {
            std::cerr << "  [heap_object] " << cfg.count << "x RAII class" << std::endl;
            DemoHeapObject(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "object_pool") {
            std::cerr << "  [object_pool] " << cfg.count << "x acquire/release" << std::endl;
            DemoObjectPool(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "custom_lifecycle") {
            std::cerr << "  [custom_lifecycle] " << cfg.count << "x delayed destruct" << std::endl;
            DemoCustomLifecycle(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "vector_growth") {
            std::cerr << "  [vector_growth] " << cfg.count << "x push_back (no reserve)" << std::endl;
            DemoVectorGrowth(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "string_sso") {
            std::cerr << "  [string_sso] crossing SSO boundary" << std::endl;
            DemoStringSSO(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "map_churn") {
            std::cerr << "  [map_churn] " << cfg.count << " insert/" << (cfg.count * 9 / 10) << " erase/" << (cfg.count / 2) << " reinsert" << std::endl;
            DemoMapChurn(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "spike") {
            std::cerr << "  [spike] 10 MiB transient allocation" << std::endl;
            DemoSpike(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "nested") {
            std::cerr << "  [nested] " << cfg.count << "x vector<string> (dual key)" << std::endl;
            DemoNested(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "realloc_cascade") {
            std::cerr << "  [realloc_cascade] resize/shrink/resize cascade" << std::endl;
            DemoReallocCascade(cfg.count);
        }
        if (cfg.mode.empty() || cfg.mode == "struct_breakdown") {
            std::cerr << "  [struct_breakdown] Per-field memory breakdown (6 scenarios)" << std::endl;
            DemoStructBreakdown(cfg.count);
        }
    };

    if (cfg.loop) {
        while (true) {
            runOnce();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(cfg.intervalMs));
        }
    }

    for (int r = 0; r < cfg.repeat; ++r) {
        runOnce();
    }

    if (ranLeak) {
        std::cerr << "\n  Leaked memory active (" << cfg.count * 4 << " KB).\n";
        std::cerr << "  Run: sudo spdiag mempoint --pid " << getpid() << "\n";
        std::cerr << "  Press ENTER to exit.\n";
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
    }

    std::cout << "mempoint_demo completed." << std::endl;
    return 0;
}

#else // !SPDIAG_ENABLE_MEMPOINT
#include <iostream>
int main() {
    std::cerr << "mempoint_demo requires SPDIAG_ENABLE_MEMPOINT (Linux + systemtap-sdt-dev)"
              << std::endl;
    return 1;
}
#endif
