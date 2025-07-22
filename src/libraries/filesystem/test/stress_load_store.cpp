// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <m/debugging/dbg_format.h>
#include <m/exception/exception.h>
#include <m/filesystem/filesystem.h>
#include <m/googletest/temporary_directory.h>
#include <m/utility/algorithm.h>
#include <m/utility/random.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

TEST(StressLoadStore, StressNSeconds)
{
    EXPECT_EQ(1, 1);

    constexpr std::size_t loader_count        = 4;
    constexpr std::size_t big_storer_count    = 1;
    constexpr std::size_t little_storer_count = 1;

    auto const td   = m::googletest::create_temporary_directory();
    auto const path = m::filesystem::make_path(td->path(), L"temporary_file");

    std::random_device rd;
    std::mt19937_64    entropy(rd());

    auto const little_file = m::random::make_unique_byte_span(20, entropy);
    auto const big_file    = m::random::make_unique_byte_span(30000, entropy);

    std::atomic<bool> start{false};
    std::atomic<bool> stop{false};

    std::atomic<uintmax_t> fault_count{0};
    std::atomic<uintmax_t> not_found_count{0};
    std::atomic<uintmax_t> load_count{};
    std::atomic<uintmax_t> store_count{};
    std::atomic<uintmax_t> load_sharing_violation_count{};
    std::atomic<uintmax_t> store_sharing_violation_count{};

    auto const loader = [&](std::filesystem::path path) {
        while (!start.load())
            start.wait(false);

        while (!stop.load())
        {
            try
            {
                auto contents = m::filesystem::load(path);
                // m::dbg_format(L"Loaded file of {} bytes", contents.size());
                load_count++;
            }
            catch (m::not_found&)
            {
                m::dbg_format(L"File was not found");
                // fault_count++;
                not_found_count++;
            }
            catch (m::sharing_violation&)
            {
                m::dbg_format(L"File was busy");
                fault_count++;
                load_sharing_violation_count++;
            }
            catch (std::exception&)
            {
                m::dbg_format(L"Caught exception while loading {}", path);
                fault_count++;
            }
        }
    };

    auto const storer = [&](std::filesystem::path path, m::unique_span<std::byte> const& span) {
        while (!start.load())
            start.wait(false);

        while (!stop.load())
        {
            try
            {
                m::filesystem::store(path, span);
                // m::dbg_format(L"Stored file of {} bytes", span.size());
                store_count++;
            }
            catch (m::sharing_violation&)
            {
                m::dbg_format(L"File was busy");
                fault_count++;
                store_sharing_violation_count++;
            }
            catch (std::exception&)
            {
                m::dbg_format(L"Caught exception while storing {}", path);
                fault_count++;
            }
        }
    };

    std::vector<std::thread> threads;

    threads.reserve(loader_count + big_storer_count + little_storer_count);

    m::n_times(loader_count, [&]() { threads.emplace_back(loader, path); });
    m::n_times(big_storer_count, [&]() { threads.emplace_back(storer, path, std::ref(big_file)); });
    m::n_times(little_storer_count,
               [&]() { threads.emplace_back(storer, path, std::ref(little_file)); });

    start.store(true);
    start.notify_all();

    std::this_thread::sleep_for(60s);

    stop.store(true);

    for (auto&& e: threads)
        e.join();

    EXPECT_EQ(fault_count.load(), 0);
}
