// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <m/atomic/atomic.h>

using namespace std::chrono_literals;

struct SomeStruct
{
    int x;
};

SomeStruct*
alloc1()
{
    auto p = new SomeStruct;

    p->x = 9;
    return p;
}

void
dealloc1(SomeStruct* ptr)
{
    delete ptr;
}

constexpr auto magicnumber = 42;

TEST(Atomic, AtomicInitializedPointer)
{
    m::atomic_pointer_with_initializer<SomeStruct*, alloc1, dealloc1> apwi;

    EXPECT_EQ(9, apwi->x);
}

TEST(Atomic, AtomicInitializedPointer2)
{
    m::atomic_pointer_with_initializer<SomeStruct*,
                                       []() {
                                           auto p = new SomeStruct;
                                           p->x   = 10;
                                           return p;
                                       },
                                       [](SomeStruct* ptr) { delete ptr; }>
        apwi;

    EXPECT_EQ(10, apwi->x);
}

TEST(Atomic, AtomicInitializedPointer3)
{
    m::atomic_pointer_with_initializer<SomeStruct*,
                                       []() {
                                           auto p = new SomeStruct;
                                           p->x   = magicnumber;
                                           return p;
                                       },
                                       [](SomeStruct* ptr) { delete ptr; }>
        apwi;

    EXPECT_EQ(magicnumber, apwi->x);
}

TEST(Atomic, AtomicInitializedPointer4)
{
    m::atomic_pointer_with_initializer<SomeStruct*,
                                       []() {
                                           auto p = new SomeStruct;
                                           p->x   = magicnumber;
                                           return p;
                                       }>
        apwi;

    EXPECT_EQ(magicnumber, apwi->x);
}
