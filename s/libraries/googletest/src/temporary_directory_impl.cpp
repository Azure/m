// Copyright (c) Microsoft Corporation. All rights reserved.

#include <chrono>
#include <filesystem>
#include <numeric>
#include <random>
#include <ratio>
#include <type_traits>

#include <m/googletest/temporary_directory.h>

#include "temporary_directory_impl.h"

m::googletest_impl::temporary_directory_impl::temporary_directory_impl(
    std::shared_ptr<m::googletest::temporary_directory_manager> const& parent,
    std::filesystem::path const&                                       parent_path,
    rng_result_type random_number,
    bool retain):
    m_temporary_directory_manager(parent),
    m_path{make_random_subdirectory(parent_path, random_number)},
    m_retain(retain)
{}

m::googletest_impl::temporary_directory_impl::~temporary_directory_impl()
{
    if (!m_retain)
        do_remove_all();
}

std::filesystem::path
m::googletest_impl::temporary_directory_impl::make_random_subdirectory(
    std::filesystem::path const& parent_path,
    rng_result_type              random_number)
{
    auto s = std::format(L"{:x}", random_number);
    auto p = parent_path / s;
    std::filesystem::create_directory(p);
    return p;
}

std::filesystem::path
m::googletest_impl::temporary_directory_impl::do_path()
{
    auto l = std::unique_lock(m_mutex);
    return m_path;
}

std::shared_ptr<m::googletest::temporary_directory>
m::googletest_impl::temporary_directory_impl::do_create(bool retain)
{
    auto l = std::unique_lock(m_mutex);

    if (auto tDM = m_temporary_directory_manager.lock())
    {
        return std::make_shared<temporary_directory_impl>(tDM, m_path, tDM->generate_random(), retain);
    }

    return nullptr;
}

bool
m::googletest_impl::temporary_directory_impl::do_retain()
{
    auto l = std::unique_lock(m_mutex);
    return m_retain;
}

bool
m::googletest_impl::temporary_directory_impl::do_retain(bool retain)
{
    auto l = std::unique_lock(m_mutex);
    return std::exchange(m_retain, retain);
}

std::uintmax_t
m::googletest_impl::temporary_directory_impl::do_remove_all()
{
    auto l = std::unique_lock(m_mutex);
    return std::filesystem::remove_all(m_path);
}
