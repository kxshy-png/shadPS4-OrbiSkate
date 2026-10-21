// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "core/platform.h"

TEST(IrqController, MatchesRequestsInsteadOfRegistrationOrder) {
    Platform::IrqController controller;
    std::vector<int> calls;
    const auto first =
        controller.RegisterOnce(Platform::InterruptId::GfxFlip, [&](auto) { calls.push_back(1); });
    const auto second =
        controller.RegisterOnce(Platform::InterruptId::GfxFlip, [&](auto) { calls.push_back(2); });
    EXPECT_NE(first, 0);
    EXPECT_NE(first, second);
    controller.Signal(Platform::InterruptId::GfxFlip, second);
    EXPECT_EQ(calls, std::vector<int>{2});
    controller.Signal(Platform::InterruptId::GfxFlip, first);
    EXPECT_EQ(calls, (std::vector<int>{2, 1}));
}

TEST(IrqController, ReusedAndUnknownRequestsDoNotConsumeLaterFlips) {
    Platform::IrqController controller;
    int first_calls{}, second_calls{}, persistent_calls{};
    controller.Register(
        Platform::InterruptId::GfxFlip, [&](auto) { ++persistent_calls; }, &persistent_calls);
    const auto first =
        controller.RegisterOnce(Platform::InterruptId::GfxFlip, [&](auto) { ++first_calls; });
    controller.Signal(Platform::InterruptId::GfxFlip, first);
    const auto second =
        controller.RegisterOnce(Platform::InterruptId::GfxFlip, [&](auto) { ++second_calls; });
    controller.Signal(Platform::InterruptId::GfxFlip, first);
    controller.Signal(Platform::InterruptId::GfxFlip, second + 1);
    EXPECT_EQ(first_calls, 1);
    EXPECT_EQ(second_calls, 0);
    EXPECT_EQ(persistent_calls, 1);
    controller.Signal(Platform::InterruptId::GfxFlip, second);
    EXPECT_EQ(second_calls, 1);
    EXPECT_EQ(persistent_calls, 2);
}

TEST(IrqController, UntaggedSignalsRetainFifoAndPersistentHandlers) {
    Platform::IrqController controller;
    std::vector<int> calls;
    int persistent_calls{};
    controller.Register(
        Platform::InterruptId::GfxFlip, [&](auto) { ++persistent_calls; }, &persistent_calls);
    controller.RegisterOnce(Platform::InterruptId::GfxFlip, [&](auto) { calls.push_back(1); });
    controller.RegisterOnce(Platform::InterruptId::GfxFlip, [&](auto) { calls.push_back(2); });
    controller.Signal(Platform::InterruptId::GfxFlip);
    controller.Signal(Platform::InterruptId::GfxFlip);
    controller.Signal(Platform::InterruptId::GfxFlip);
    EXPECT_EQ(calls, (std::vector<int>{1, 2}));
    EXPECT_EQ(persistent_calls, 3);
}

TEST(IrqController, ConcurrentContextsRetainTheirCallbacks) {
    for (int iteration = 0; iteration < 32; ++iteration) {
        Platform::IrqController controller;
        std::barrier start(8);
        std::array<std::atomic<int>, 8> calls{};
        std::array<std::thread, 8> threads;
        for (int index = 0; index < 8; ++index) {
            threads[index] = std::thread([&, index] {
                const auto irq = index == 7 ? Platform::InterruptId::GfxFlip
                                            : static_cast<Platform::InterruptId>(index);
                start.arrive_and_wait();
                const auto request =
                    controller.RegisterOnce(irq, [&, index](auto) { ++calls[index]; });
                start.arrive_and_wait();
                controller.Signal(irq, request);
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        for (const auto& count : calls) {
            EXPECT_EQ(count.load(), 1);
        }
    }
}
