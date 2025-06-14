// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <mutex>

#include <m/byte_streams/byte_streams.h>
#include <m/byte_streams/memory_based_byte_streams.h>
#include <m/io/units.h>

namespace m
{
    namespace byte_streams_impl
    {
        //
        // Memory-based Read Only Random Accessible and Sequential byte stream
        //
        // Phew that's a mouthful.
        //
        class memory_ro_ra_seq : public m::byte_streams::memory_based_byte_stream
        {
        public:
            // memory_ro_ra_seq(std::span<std::byte const> const&);
            memory_ro_ra_seq(std::unique_ptr<std::byte[]>&&, size_t count);
            memory_ro_ra_seq(memory_ro_ra_seq const&) = delete;
            virtual ~memory_ro_ra_seq()               = default;

        protected:
            // byte_streams::seq_in
            std::size_t
            do_read(std::span<std::byte>& span) override;

            // byte_streams::ra_in
            std::size_t
            do_read(io::position_t p, std::span<std::byte>& span) override;

        private:
            std::mutex                   m_mutex;
            std::unique_ptr<std::byte[]> m_array; // for lifetime management only
            io::position_t               m_current_position;
            std::span<std::byte const>   m_span; // always use for access
        };
    } // namespace byte_streams_impl
} // namespace m
