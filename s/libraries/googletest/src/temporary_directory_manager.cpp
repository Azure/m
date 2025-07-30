// Copyright (c) Microsoft Corporation. All rights reserved.

#include <chrono>
#include <filesystem>
#include <numeric>
#include <random>
#include <ratio>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/googletest/temporary_directory.h>
#include <m/strings/convert.h>

#include "temporary_directory_impl.h"

m::googletest::temporary_directory_manager::temporary_directory_manager(
    std::filesystem::path const& rootPath,
    rng_type&                    generator):
    m_generator(generator())
{
    auto n = std::chrono::utc_clock::now();
    auto r =
        (generate_random_no_lock() & 0xffffu); // limit the size of the random number, with the date
                                               // and time we don't need such a huge random factor
    auto prefix = std::format(L"{:%Y%m%d%H%M%S}-{:x}", n, r);
    m_path      = rootPath / prefix;
    std::filesystem::create_directory(m_path);
}

//
// Remember not to use with a seeded generator.
//
std::shared_ptr<m::googletest::temporary_directory_manager>
m::googletest::temporary_directory_manager::racy_initialize_once(
    std::atomic<std::shared_ptr<temporary_directory_manager>>& shared,
    std::filesystem::path const&                               rootPath,
    rng_type&                                                  generator)
{
    auto result = shared.load(std::memory_order_acquire);

    if (result)
        return result;

    do
    {
        auto desired = std::make_shared<temporary_directory_manager>(rootPath, generator);

        if (shared.compare_exchange_strong(result, desired, std::memory_order_acq_rel))
            return desired;

        //
        // /Should/ never loop. Should.
    } while (!result);

    return shared.load(std::memory_order_acquire);
}

m::googletest::temporary_directory_manager::~temporary_directory_manager()
{
    // Not cleaning up, intentionally at this time, so that the
    // temporary files can be inspected after a failed run. Really
    // there should some way to mark whether to preserve the
    // temporary files or not.
}

std::shared_ptr<m::googletest::temporary_directory>
m::googletest::temporary_directory_manager::create(bool retain)
{
    uint64_t              random_number{};
    std::filesystem::path parent_path;

    {
        auto l        = std::unique_lock(m_mutex);
        random_number = generate_random_no_lock();
        parent_path   = m_path;
    }

    //
    // It is an explicit choice to leave the scope with the lock before
    // the return so as to avoid holding the lock while allocating
    // memory and copying things.
    //

    return std::make_shared<m::googletest_impl::temporary_directory_impl>(
        shared_from_this(), parent_path, random_number, retain);
}

void
m::googletest::temporary_directory_manager::on_test_completion()
{
    auto l = std::unique_lock(m_mutex);

    if (testing::UnitTest::GetInstance()->Failed())
    {
        // Currently we don't associate directories with tests so we just leave everything
        // in place

        return;
    }

    using string_type      = typename std::filesystem::path::string_type;
    using value_type       = typename std::filesystem::path::value_type;
    using string_view_type = std::basic_string_view<value_type>;

    auto lastPart = m_path.filename();

    auto prefix = string_type(string_view_type(
#ifdef WIN32
        L"DONE-"
#else
        "DONE-"
#endif
        ));

    auto newname = prefix + lastPart.c_str();

    auto newpath = m_path.parent_path() / std::filesystem::path(newname);

    // just rename
    std::filesystem::rename(m_path, newpath);
}

m::googletest::temporary_directory_manager::rng_result_type
m::googletest::temporary_directory_manager::generate_random()
{
    auto l = std::unique_lock(m_mutex);
    return generate_random_no_lock();
}

m::googletest::temporary_directory_manager::rng_result_type
m::googletest::temporary_directory_manager::generate_random_no_lock()
{
    return m_generator();
}

std::shared_ptr<m::googletest::temporary_directory>
m::googletest::create_temporary_directory(bool retain)
{
    static struct global_tdm
    {
        // Maintainer: remember that members are initialized in *declaration* order,
        // so it's important that root_path is declared before tdm, and that
        // is before generator and that generator is before tdm. kind of crazy.
        global_tdm(): generator(rd()), root_path(testing::TempDir())
        {
            auto x = std::make_shared<m::googletest::temporary_directory_manager>(
                std::ref(root_path), std::ref(generator));

            using std::swap;
            swap(x, tdm);
        }
        std::mutex                                           m;
        std::random_device                                   rd;
        m::googletest::temporary_directory_manager::rng_type generator;
        std::filesystem::path                                root_path;

        std::shared_ptr<m::googletest::temporary_directory_manager> tdm;
    } s_global_tdm;

    auto l = std::unique_lock(s_global_tdm.m);
    return s_global_tdm.tdm->create(retain);
}
