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

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include "runtime/plugins/ob_point/ob_point_types.h"
#include "spdiag/log_macros.h"

namespace SpDiag {

inline bool StartsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

class FrameFilter {
public:
    [[nodiscard]] std::vector<FrameInfo> Apply(const std::vector<FrameInfo>& in) const {
        auto out = DropMappingSymbols(in);
        out = ResolveWellKnownStubs(std::move(out));
        out = CollapseStlGlue(std::move(out));
        out = CollapseThreadTrampoline(std::move(out));
        out = DedupAdjacent(std::move(out));
        return out;
    }

    static bool IsMappingSymbol(std::string_view s) {
        if (s.size() < 2) return false;
        if (s[0] != '$') return false;
        char c = s[1];
        return c == 'x' || c == 'd' || c == 'a' || c == 't' || c == '.';
    }

    static bool IsStlGlue(std::string_view s) {
        return StartsWith(s, "std::_Vector_base<")
            || StartsWith(s, "std::__new_allocator<")
            || StartsWith(s, "std::__uninitialized_")
            || StartsWith(s, "std::_Construct<")
            || StartsWith(s, "std::allocator_traits<")
            || StartsWith(s, "__gnu_cxx::new_allocator<")
            || StartsWith(s, "std::__invoke_impl")
            || StartsWith(s, "std::__invoke_result")
            || StartsWith(s, "std::__invoke<")
            || StartsWith(s, "std::invoke<")
            || StartsWith(s, "std::get<")
            || StartsWith(s, "std::tuple_element<")
            || StartsWith(s, "std::thread::_Invoker<")
            || StartsWith(s, "std::thread::_State_impl<")
            || StartsWith(s, "std::__cxx11::basic_string<")
            || StartsWith(s, "std::__shared_ptr<")
            || StartsWith(s, "std::__shared_count<");
    }

    static bool IsThreadTrampoline(std::string_view s) {
        return StartsWith(s, "std::thread::_Invoker<")
            || StartsWith(s, "std::thread::_State_impl<")
            || StartsWith(s, "std::__invoke_impl")
            || StartsWith(s, "std::__invoke_result")
            || StartsWith(s, "std::__invoke<")
            || s == "start_thread"
            || s == "clone3"
            || s == "clone"
            || s == "__clone"
            || s == "[thread entry]";
    }

    static bool IsAllocatorFunc(std::string_view s) {
        return StartsWith(s, "operator new")
            || StartsWith(s, "operator delete")
            || StartsWith(s, "malloc")
            || StartsWith(s, "calloc")
            || StartsWith(s, "realloc")
            || StartsWith(s, "free");
    }

    static bool IsLibraryModule(std::string_view s) {
        return s.find("libc.so") != std::string_view::npos
            || s.find("libstdc++") != std::string_view::npos
            || s.find("ld-linux") != std::string_view::npos;
    }

    static const FrameInfo* PickFrameSummary(const std::vector<FrameInfo>& v) {
        for (const auto& f : v) {
            if (f.func.empty()) continue;
            if (IsAllocatorFunc(f.func)) continue;
            if (IsStlGlue(f.func)) continue;
            if (IsThreadTrampoline(f.func)) continue;
            if (IsLibraryModule(f.module)) continue;
            return &f;
        }
        return v.empty() ? nullptr : &v.front();
    }

    static bool IsExactSymbol(std::string_view sym, std::string_view name) {
        if (sym == name) return true;
        if (StartsWith(sym, name) && sym.size() > name.size() && sym[name.size()] == '(') return true;
        return false;
    }

    static bool IsNoiseFrame(std::string_view sym) {
        static constexpr std::string_view kSubstr[] = {
            "std::_Vector_base<",
            "std::__new_allocator<",
            "__gnu_cxx::new_allocator<",
            "std::allocator_traits<",
            "operator new",
            "__builtin_operator_new",
            "std::make_unique",
            "std::__make_unique",
            "std::make_shared",
            "std::__make_shared",
            "_M_allocate",
        };
        for (auto s : kSubstr) {
            if (sym.find(s) != std::string_view::npos) return true;
        }
        if (sym.find("std::allocator<") != std::string_view::npos &&
            sym.find("::allocate") != std::string_view::npos) return true;
        if (IsExactSymbol(sym, "malloc") ||
            IsExactSymbol(sym, "calloc") ||
            IsExactSymbol(sym, "realloc")) return true;
        return false;
    }

    static std::string_view PickHeaderFrame(const std::vector<FrameInfo>& frames) {
        std::string_view fallback;
        for (const auto& f : frames) {
            if (IsNoiseFrame(f.func)) continue;
            if (fallback.empty()) fallback = f.func;
            if (!StartsWith(f.func, "std::")) return f.func;
        }
        if (!fallback.empty()) return fallback;
        return frames.empty() ? std::string_view() : std::string_view(frames.front().func);
    }

private:
    [[nodiscard]] std::vector<FrameInfo> DropMappingSymbols(const std::vector<FrameInfo>& in) const {
        std::vector<FrameInfo> out;
        for (const auto& f : in) {
            if (!IsMappingSymbol(f.func)) {
                out.push_back(f);
            }
        }
        return out;
    }

    static bool IsUnresolvedOffset(std::string_view s) {
        auto pos = s.find("+0x");
        if (pos == std::string_view::npos) return false;
        for (auto c : s) {
            if (c == '.') return true;
        }
        return false;
    }

    static std::string ShortenLibName(std::string_view s) {
        if (s.find("libc.so") != std::string_view::npos) return "[libc]";
        if (s.find("libstdc++") != std::string_view::npos) return "[libstdc++]";
        return std::string(s);
    }

    static bool TryResolveKnownStub(const std::string& func, std::string& outName) {
        auto plusPos = func.rfind("+0x");
        if (plusPos == std::string::npos) return false;
        std::string modName = func.substr(0, plusPos);
        uint64_t offsetVal = 0;
        try {
            offsetVal = std::stoull(func.substr(plusPos + 1), nullptr, 16);
        } catch (const std::exception& e) {
            SPDIAG_LOG_ERROR(LogModule::CLI, "Parse offset '%s' failed: %s", 
                             func.c_str(), e.what());
            return false;
        }

        static const std::unordered_map<std::string,
            std::unordered_map<uint64_t, std::string>> kWellKnown = {
            {"libc.so.6", {
                {0x7fbb4, "start_thread"},
                {0xe7f4c, "__clone3"},
            }},
        };

        auto mIt = kWellKnown.find(modName);
        if (mIt != kWellKnown.end()) {
            auto oIt = mIt->second.find(offsetVal);
            if (oIt != mIt->second.end()) {
                outName = oIt->second;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::vector<FrameInfo> ResolveWellKnownStubs(std::vector<FrameInfo> in) const {
        for (auto& f : in) {
            if (!IsUnresolvedOffset(f.func)) continue;
            std::string resolved;
            if (TryResolveKnownStub(f.func, resolved)) {
                f.func = resolved;
            } else {
                f.func = ShortenLibName(f.func);
            }
        }
        return in;
    }

    [[nodiscard]] std::vector<FrameInfo> CollapseStlGlue(std::vector<FrameInfo> in) const {
        std::vector<FrameInfo> out;
        for (size_t i = 0; i < in.size(); ++i) {
            if (IsStlGlue(in[i].func)) {
                if (i > 0 && !IsStlGlue(in[i - 1].func)) {
                    continue;
                }
            }
            out.push_back(std::move(in[i]));
        }
        return out;
    }

    [[nodiscard]] std::vector<FrameInfo> CollapseThreadTrampoline(std::vector<FrameInfo> in) const {
        std::vector<FrameInfo> out;
        bool merged = false;
        for (auto it = in.rbegin(); it != in.rend(); ++it) {
            if (IsThreadTrampoline(it->func)) {
                if (!merged) {
                    FrameInfo f;
                    f.func = "[thread entry]";
                    out.push_back(std::move(f));
                    merged = true;
                }
            } else {
                out.push_back(*it);
            }
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    [[nodiscard]] std::vector<FrameInfo> DedupAdjacent(std::vector<FrameInfo> in) const {
        in.erase(std::unique(in.begin(), in.end(),
            [](const FrameInfo& a, const FrameInfo& b) {
                return a.func == b.func && a.line == b.line;
            }), in.end());
        return in;
    }
};

}
