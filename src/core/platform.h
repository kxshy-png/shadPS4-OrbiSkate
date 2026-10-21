// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/singleton.h"
#include "common/types.h"

#include <magic_enum/magic_enum.hpp>

#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace Platform {

enum class InterruptId : u32 {
    Compute0RelMem = 0x00,
    Compute1RelMem = 0x01,
    Compute2RelMem = 0x02,
    Compute3RelMem = 0x03,
    Compute4RelMem = 0x04,
    Compute5RelMem = 0x05,
    Compute6RelMem = 0x06,
    GfxEop = 0x40,
    GfxFlip = 0x08,
    GpuIdle = 0x09,

    InterruptIdMax = 0x40, ///< Max possible value (GfxEop)
};

using IrqHandler = std::function<void(InterruptId)>;

struct IrqController {
    u64 RegisterOnce(InterruptId irq, IrqHandler handler) {
        ASSERT_MSG(static_cast<u32>(irq) <= static_cast<u32>(InterruptId::InterruptIdMax),
                   "Invalid IRQ number");
        auto& ctx = irq_contexts[static_cast<u32>(irq)];
        std::unique_lock lock{ctx.m_lock};
        const u64 request_id = ++ctx.last_request_id;
        ASSERT_MSG(request_id != 0, "IRQ request identifier exhausted");
        ctx.one_time_subscribers.emplace_back(request_id, std::move(handler));
        return request_id;
    }

    void Register(InterruptId irq, IrqHandler handler, void* uid) {
        ASSERT_MSG(static_cast<u32>(irq) <= static_cast<u32>(InterruptId::InterruptIdMax),
                   "Invalid IRQ number");
        auto& ctx = irq_contexts[static_cast<u32>(irq)];

        std::unique_lock lock{ctx.m_lock};
        ASSERT_MSG(ctx.persistent_handlers.find(uid) == ctx.persistent_handlers.cend(),
                   "The handler is already registered!");
        ctx.persistent_handlers.emplace(uid, handler);
    }

    void Unregister(InterruptId irq, void* uid) {
        ASSERT_MSG(static_cast<u32>(irq) <= static_cast<u32>(InterruptId::InterruptIdMax),
                   "Invalid IRQ number");
        auto& ctx = irq_contexts[static_cast<u32>(irq)];
        std::unique_lock lock{ctx.m_lock};
        ctx.persistent_handlers.erase(uid);
    }

    void Signal(InterruptId irq, u64 request_id = 0) {
        ASSERT_MSG(static_cast<u32>(irq) <= static_cast<u32>(InterruptId::InterruptIdMax),
                   "Unexpected IRQ signaled");
        auto& ctx = irq_contexts[static_cast<u32>(irq)];
        std::unique_lock lock{ctx.m_lock};

        const auto handler = request_id == 0
                                 ? ctx.one_time_subscribers.begin()
                                 : std::ranges::find(ctx.one_time_subscribers, request_id,
                                                     &std::pair<u64, IrqHandler>::first);
        if (request_id != 0 && handler == ctx.one_time_subscribers.end()) {
            return;
        }

        LOG_TRACE(Core, "IRQ signaled: {}", magic_enum::enum_name(irq));

        for (auto& [uid, h] : ctx.persistent_handlers) {
            h(irq);
        }

        if (handler != ctx.one_time_subscribers.end()) {
            handler->second(irq);
            ctx.one_time_subscribers.erase(handler);
        }
    }

private:
    struct IrqContext {
        std::unordered_map<void*, IrqHandler> persistent_handlers{};
        std::deque<std::pair<u64, IrqHandler>> one_time_subscribers{};
        u64 last_request_id{};
        std::mutex m_lock{};
    };
    std::array<IrqContext, static_cast<u32>(InterruptId::InterruptIdMax) + 1> irq_contexts{};
};

using IrqC = Common::Singleton<IrqController>;

} // namespace Platform
