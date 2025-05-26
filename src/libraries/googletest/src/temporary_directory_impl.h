// Copyright (c) Microsoft Corporation. All rights reserved.

#include <chrono>
#include <filesystem>
#include <numeric>
#include <random>
#include <ratio>
#include <type_traits>

#include <m/googletest/temporary_directory.h>

namespace m
{
    namespace googletest_impl
    {
        class temporary_directory_impl :
            public m::googletest::temporary_directory,
            std::enable_shared_from_this<temporary_directory_impl>
        {
            using rng_result_type =
                typename m::googletest::temporary_directory_manager::rng_result_type;

        public:
            temporary_directory_impl(
                std::shared_ptr<m::googletest::temporary_directory_manager> const&,
                std::filesystem::path const& parentPath,
                rng_result_type              randomNumber,
                bool                         retain);

        // protected:
            std::filesystem::path
            make_random_subdirectory(std::filesystem::path const& parentPath,
                                     rng_result_type              randomNumber);

            ~temporary_directory_impl();

            std::shared_ptr<temporary_directory>
            do_create(bool retain) override;

            bool
            do_retain() override;

            bool
            do_retain(bool retain) override;

            std::filesystem::path
            do_path() override;

            std::uintmax_t
            do_remove_all() override;

            rng_result_type
            generate_random() const;

        private:
            std::mutex                                                m_mutex;
            std::weak_ptr<m::googletest::temporary_directory_manager> m_temporary_directory_manager;
            std::filesystem::path                                     m_path;
            bool                                                      m_retain;
        };
    } // namespace googletest_impl
} // namespace m
