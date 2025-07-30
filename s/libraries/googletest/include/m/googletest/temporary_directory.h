// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace m
{
    namespace googletest
    {
        class temporary_directory;

        std::shared_ptr<m::googletest::temporary_directory>
        create_temporary_directory(bool retain = false);

        class temporary_directory_manager :
            public std::enable_shared_from_this<temporary_directory_manager>
        {
        protected:
            // The temporary directories are named using random numbers.
            // 
            // rng_type is the type alias used as the randon number generator
            // (generator as in the sense of "Random number engines" described
            // at https://en.cppreference.com/w/cpp/numeric/random.html)
            // 
            // or, https://en.cppreference.com/w/cpp/named_req/RandomNumberEngine.html
            // 
            using rng_type        = std::mt19937_64;

        public:
            // The RandomNumberEngine named type calls for E::result_type to be
            // the type that the engine generates.
            // 
            // The engine that this was originally coded with is the Mersenne Twister
            // algorithm, generating an unsigned 64 bit output value. This is large
            // enough that with a simple test for collision after generation, we'll
            // just use it to generate the name of the temporary directory under
            // the unique name that's generated for the temporary directory manager.
            // 
            // If you feel compelled to change the rng_type, just don't go to something
            // overly small. The value is truncated when generating the name of the
            // directory for the temporary_directory_manager but from a C++ language
            // perspective is just passed along by value. The truncation is done by
            // a bitwise AND with an integral value; if that has a mistake you'll have
            // a hard time figuring it out other than by inspection. It's difficult to
            // unit test the manager itself, and realistically an enormous number of
            // directories would have to be created to start hitting any collisions.
            //

            using rng_result_type = typename rng_type::result_type;
            temporary_directory_manager(std::filesystem::path const& rootPath, rng_type& generator);
            ~temporary_directory_manager();

            /// <summary>
            /// </summary>
            /// <param name="retain">Boolean indicator of whether the directory
            /// contents should be retained when the temporary directory object
            /// is destroyed.</param>
            /// <returns>std::shared_ptr<> used to manage the lifetime of the
            /// temporary_directory object.</returns>
            std::shared_ptr<temporary_directory>
            create(bool retain = true);

            void
            on_test_completion();

            rng_result_type
            generate_random();

        protected:
            static std::shared_ptr<temporary_directory_manager>
            racy_initialize_once(std::atomic<std::shared_ptr<temporary_directory_manager>>&,
                                 std::filesystem::path const& rootPath,
                                 rng_type&                    generator);

            temporary_directory_manager(temporary_directory_manager const&) = delete;
            void
            operator=(temporary_directory_manager const&) = delete;

            std::mutex                                      m_mutex;
            std::filesystem::path                           m_path;
            rng_type                                        m_generator;
            std::vector<std::weak_ptr<temporary_directory>> m_temp_directories;

            rng_result_type
            generate_random_no_lock();

            friend class temporary_directory;

            friend std::shared_ptr<m::googletest::temporary_directory>
            create_temporary_directory(bool retain);
        };

        /// <summary>
        /// 
        /// The `temporary_directory` class is an interface type which
        /// defines a C++ interface for working with a temporary directory
        /// within a GoogleTest test.
        ///
        /// Its lifetime is managed by virtue of getting the temporary
        /// directory object in a std::shared_ptr<temporary_directory>,
        /// and depending on whether the `retain` property is set to `true`
        /// or `false` on destruction, the contents of the temporary
        /// directory will be retained or deleted (best effort).
        ///
        /// Generally speaking the pattern should be to create the temporary
        /// directory with retain=true, do your test work, and then if the
        /// test is successful, change the retain to false so that the
        /// destruction of the temporary directory cleans up any leftover
        /// files.
        ///
        /// In the future, this will integrate with a type that derives from
        /// the GoogleTest `testing::EmptyTestEventListener` class and have an
        /// option to automatically perform cleanup and perhaps collect
        /// or report left-over pieces? (Like maybe create a zip file of
        /// what was left or at least some report of all the files, their
        /// sizes etc.)
        /// 
        /// The temporary_directory_manager has weak references to each of
        /// the temporary_directory objects it has handed out. I've gone
        /// back and forth on whether to enact this kind of policy at the
        /// directory level (where this is a useful feature beyond just
        /// testing perhaps) vs at the manager level assuming that this is
        /// a set of code directed squarely at making testing as streamlined
        /// as possible.
        /// 
        /// </summary>
        class temporary_directory
        {
        public:
            /// <summary>
            /// Creates a temporary_directory object that is a subdirectory
            /// of the `this` temporary_directory object.
            ///
            /// Its name is a random number and it is subject to cleanup
            /// when the last reference to the returned temporary_directory
            /// is destroyed.
            /// </summary>
            /// <returns>`std::shared_ptr<>` managing the lifetime of the
            /// temporary directory created. The returned shared pointer is
            /// the one and only (strong) reference to the temporary
            /// directory.
            ///
            /// When there are no further references on the
            /// temporary_directory instance, it is destroyed and the
            /// directory in the filesystem will be cleared or retained as
            /// per policy that was set for the temporary directory.</returns>
            std::shared_ptr<temporary_directory>
            create(bool retain = false)
            {
                return do_create(retain);
            }

            bool
            retain()
            {
                return do_retain();
            }

            bool
            retain(bool retain)
            {
                return do_retain(retain);
            }

            std::filesystem::path
            path()
            {
                return do_path();
            }

            /// <summary>
            /// Removes the entire contents of the temporary directory, as
            /// per the std::filesystem::remove_all() function.
            ///
            /// This has no bearing on the `retain` property of the
            /// temporary directory.
            /// </summary>
            /// <returns>The number of filesystem objects removed. See the
            /// documentation for std::filesystem::remove_all() for specifics
            /// and semantics. Ideally, do not use other than for debugging or
            /// informative logging.</returns>
            std::uintmax_t
            remove_all()
            {
                return do_remove_all();
            }

        protected:
            virtual ~temporary_directory() {}

            virtual std::shared_ptr<temporary_directory>
            do_create(bool retain) = 0;

            virtual bool
            do_retain() = 0;

            virtual bool
            do_retain(bool retain) = 0;

            virtual std::filesystem::path
            do_path() = 0;

            virtual std::uintmax_t
            do_remove_all() = 0;
        };
    } // namespace googletest
} // namespace m
