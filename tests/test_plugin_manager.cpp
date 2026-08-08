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

#include <gtest/gtest.h>

#include "src/runtime/plugin_manager.h"

namespace {

struct TestConfig : SpDiag::SpDiagPluginConfig {
    const char* ConfigTypeId() const override { return "test"; }
};

class TestPlugin final : public SpDiag::SpDiagPluginInterface {
public:
    explicit TestPlugin(std::string name = "test_plugin") : name_(std::move(name)) {}

    SpDiag::SpDiagPluginMeta Metadata() const override {
        SpDiag::SpDiagPluginMeta meta;
        meta.name = name_;
        meta.version = "1.0.0";
        meta.resourceReq.mapMemoryKb = 16;
        meta.resourceReq.maxProbes = 4;
        meta.resourceReq.maxChannels = 1;
        return meta;
    }

    bool Init(const SpDiag::SpDiagPluginConfig&) override { return true; }
    bool Start() override { return true; }
    bool Stop() override { return true; }
    bool Destroy() override { return true; }
    bool Reconfig(const SpDiag::SpDiagPluginConfig&) override { return true; }
    bool Query(const SpDiag::SpDiagQueryFilter&, SpDiag::SpDiagResultSet&) override { return true; }
    SpDiag::SpDiagHealthInfo Health() const override { return {}; }

private:
    std::string name_;
};

// Plugin whose map-memory requirement exceeds the manager budget {4096, 256, 64}.
// Exercises the private CheckResourceBudget through the public RegisterPlugin API.
class OverBudgetPlugin final : public SpDiag::SpDiagPluginInterface {
public:
    SpDiag::SpDiagPluginMeta Metadata() const override {
        SpDiag::SpDiagPluginMeta meta;
        meta.name = "over_budget_plugin";
        meta.version = "1.0.0";
        meta.resourceReq.mapMemoryKb = 8192;  // exceeds budget cap of 4096
        meta.resourceReq.maxProbes = 4;
        meta.resourceReq.maxChannels = 1;
        return meta;
    }

    bool Init(const SpDiag::SpDiagPluginConfig&) override { return true; }
    bool Start() override { return true; }
    bool Stop() override { return true; }
    bool Destroy() override { return true; }
    bool Reconfig(const SpDiag::SpDiagPluginConfig&) override { return true; }
    bool Query(const SpDiag::SpDiagQueryFilter&, SpDiag::SpDiagResultSet&) override { return true; }
    SpDiag::SpDiagHealthInfo Health() const override { return {}; }
};

// Records plugin state-change notifications so transitions can be asserted.
class StateChangeRecorder : public SpDiag::PluginEventListener {
public:
    struct Transition {
        std::string name;
        SpDiag::PluginState from;
        SpDiag::PluginState to;
    };

    void OnPluginStateChanged(const std::string& name, SpDiag::PluginState from, SpDiag::PluginState to) override {
        transitions_.push_back({name, from, to});
    }

    void OnPluginError(const std::string& name, const std::string& error) override {
        (void)name;
        (void)error;
    }

    std::vector<Transition> transitions_;
};

}

TEST(PluginManagerTest, RegisterInitStartStopDestroy) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);

    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>()));

    auto config = std::make_shared<TestConfig>();
    EXPECT_TRUE(manager.InitPlugin("test_plugin", config));
    EXPECT_TRUE(manager.StartPlugin("test_plugin"));
    EXPECT_TRUE(manager.StopPlugin("test_plugin"));
    EXPECT_TRUE(manager.DestroyPlugin("test_plugin"));
}

TEST(PluginManagerTest, DetectProbeConflict) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);
    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>()));

    SpDiag::ProbeDesc probe;
    probe.type = SpDiag::ProbeType::SOFTWARE;
    probe.symbolName = "probe_fn";

    SpDiag::ProbeHandle h1 = 0;
    EXPECT_TRUE(manager.ReserveProbe("test_plugin", probe, h1));
    EXPECT_GT(h1, 0u);

    SpDiag::ProbeHandle h2 = 0;
    EXPECT_FALSE(manager.ReserveProbe("test_plugin", probe, h2));
    EXPECT_TRUE(manager.ReleaseProbe("test_plugin", h1));
}

TEST(PluginManagerTest, StartAllStopAllDestroyAllWithMultiplePlugins) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);

    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>("alpha")));
    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>("beta")));

    auto config = std::make_shared<TestConfig>();
    EXPECT_TRUE(manager.InitPlugin("alpha", config));
    EXPECT_TRUE(manager.InitPlugin("beta", config));

    EXPECT_TRUE(manager.StartAll());
    // After StartAll both are RUNNING; a direct StartPlugin is now an invalid transition.
    EXPECT_FALSE(manager.StartPlugin("alpha"));

    EXPECT_TRUE(manager.StopAll());
    // After StopAll both are STOPPED; a direct StopPlugin is now an invalid transition.
    EXPECT_FALSE(manager.StopPlugin("beta"));

    EXPECT_TRUE(manager.DestroyAll());
}

TEST(PluginManagerTest, StartPluginOnRunningPluginRejected) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);
    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>()));

    auto config = std::make_shared<TestConfig>();
    EXPECT_TRUE(manager.InitPlugin("test_plugin", config));
    EXPECT_TRUE(manager.StartPlugin("test_plugin"));
    // RUNNING -> RUNNING is not a valid transition.
    EXPECT_FALSE(manager.StartPlugin("test_plugin"));
}

TEST(PluginManagerTest, RegisterPluginRejectsOverBudgetResources) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);
    // OverBudgetPlugin demands 8192 KB map memory, above the 4096 KB budget.
    EXPECT_FALSE(manager.RegisterPlugin(std::make_unique<OverBudgetPlugin>()));
}

TEST(PluginManagerTest, FactoryMaterializesPluginViaEnsurePlugin) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);

    manager.RegisterFactory("test_plugin", []() -> SpDiag::PluginPtr {
        return std::make_unique<TestPlugin>();
    });

    auto config = std::make_shared<TestConfig>();
    EXPECT_TRUE(manager.EnsurePlugin("test_plugin", config));

    // The plugin is now RUNNING and queryable.
    SpDiag::SpDiagResultSet result;
    SpDiag::SpDiagQueryFilter filter;
    EXPECT_TRUE(manager.QueryPlugin("test_plugin", filter, result));

    // A second EnsurePlugin on an already-RUNNING plugin is a no-op success.
    EXPECT_TRUE(manager.EnsurePlugin("test_plugin", config));
}

TEST(PluginManagerTest, EventListenerObservesStateTransitions) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);

    StateChangeRecorder listener;
    manager.AddEventListener(&listener);

    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>()));
    auto config = std::make_shared<TestConfig>();
    EXPECT_TRUE(manager.InitPlugin("test_plugin", config));
    EXPECT_TRUE(manager.StartPlugin("test_plugin"));

    ASSERT_EQ(listener.transitions_.size(), 2u);
    EXPECT_EQ(listener.transitions_[0].name, "test_plugin");
    EXPECT_EQ(listener.transitions_[0].from, SpDiag::PluginState::REGISTERED);
    EXPECT_EQ(listener.transitions_[0].to, SpDiag::PluginState::INITIALIZED);
    EXPECT_EQ(listener.transitions_[1].from, SpDiag::PluginState::INITIALIZED);
    EXPECT_EQ(listener.transitions_[1].to, SpDiag::PluginState::RUNNING);

    manager.RemoveEventListener(&listener);
}

TEST(PluginManagerTest, QueryHealthAndSinkOnRegisteredPlugin) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);
    EXPECT_TRUE(manager.RegisterPlugin(std::make_unique<TestPlugin>()));

    auto config = std::make_shared<TestConfig>();
    EXPECT_TRUE(manager.InitPlugin("test_plugin", config));

    SpDiag::SpDiagResultSet result;
    SpDiag::SpDiagQueryFilter filter;
    EXPECT_TRUE(manager.QueryPlugin("test_plugin", filter, result));

    SpDiag::SpDiagHealthInfo health;
    EXPECT_TRUE(manager.GetPluginHealth("test_plugin", health));

    // RegisterPlugin creates the channel + sink, so the sink must be available.
    SpDiag::DataSink* sink = manager.GetPluginSink("test_plugin");
    EXPECT_NE(sink, nullptr);
}

TEST(PluginManagerTest, UnknownPluginOperationsReturnFalse) {
    SpDiag::SpDiagProcessor processor;
    SpDiag::SpDiagPluginManager manager(processor);

    SpDiag::SpDiagResultSet result;
    SpDiag::SpDiagQueryFilter filter;
    EXPECT_FALSE(manager.QueryPlugin("nope", filter, result));

    SpDiag::SpDiagHealthInfo health;
    EXPECT_FALSE(manager.GetPluginHealth("nope", health));
    EXPECT_EQ(manager.GetPluginSink("nope"), nullptr);

    EXPECT_FALSE(manager.StartPlugin("nope"));
    EXPECT_FALSE(manager.StopPlugin("nope"));
    EXPECT_FALSE(manager.DestroyPlugin("nope"));
}
