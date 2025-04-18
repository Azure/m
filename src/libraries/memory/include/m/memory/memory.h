// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <type_traits>

#include <m/byte_streams/byte_streams.h>
#include <m/math/math.h>

namespace m
{
    //
    // Really, the notion here of "Random access stream and a position"
    // should be somehow abstracted into a "loading context", and then
    // this same code would apply to "a sequential stream at its current
    // position" equally well to a random access stream at a given
    // position.
    //
    // I don't know how to do that today without introducing some kind
    // of formal interface which would introduce serious inefficiency
    // both in terms of levels of indirection at runtime as well as
    // coding complexity. TBD. It would be better not to have notions
    // of random access byte streams at this point in the namespace,
    // but otherwise this general "memory" concept will be buried in
    // the byte_stream namespace.
    //
    // TODO: Require that T be trivially copyable, using a Concept. This
    // can be done easily today using SNIFAE but I wanted to hold off
    // for use of better practices. The intent of these classes are
    // decoding PE32 files and the types are, in general, DWORDs,
    // WORDs, and character strings so this is not intended for general
    // purpose serialization / deserialization support as of yet.
    //
    template <typename T, typename SourceT>
    T
    load_from(SourceT s, io::position_t p)
    {
        T v{};
        if (s->read(p, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
            throw std::runtime_error("end of file");
        return v;
    }

    template <typename T, typename SourceT>
    void
    load_into(T& v, SourceT s, io::position_t p)
    {
        if (s->read(p, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
            throw std::runtime_error("end of file");
    }

    template <typename T, typename SourceT>
    void
    load_into(T& v, SourceT s, io::position_t origin, std::size_t limit)
    {
        if (s->read(origin, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
            throw std::runtime_error("end of file");
    }

    template <typename SourceT>
    class load_from_position_context
    {
    public:
        using offset_t   = io::offset_t;
        using position_t = io::position_t;

        load_from_position_context(SourceT     s,
                                   position_t  origin,
                                   std::size_t limit = (std::numeric_limits<std::size_t>::max)()):
            m_s(s), m_origin(origin), m_limit(limit)
        {}

        template <typename T>
        void
        load_into(T& v, offset_t offset) const
        {

            m::load_into(v, m_s, m_origin + offset, m_limit);
        }

    private:
        SourceT     m_s;
        position_t  m_origin;
        std::size_t m_limit;
    };

    template <typename S>
    load_from_position_context(S, m::io::position_t, std::size_t)
        -> load_from_position_context<S>;

    template <typename S>
    load_from_position_context(S, m::io::position_t) -> load_from_position_context<S>;

    template <typename SourceT, typename TargetT>
    using data_member_loader_t = void (*)(TargetT&, load_from_position_context<SourceT> const&);

    template <typename SourceT, typename TargetT>
    void
    load_data_members(load_from_position_context<SourceT> const&        lfpc,
                      TargetT&                                          target,
                      std::span<data_member_loader_t<SourceT, TargetT>> span)
    {
        for (auto&& f: span)
        {
            f(target, lfpc);
        }
    }

    namespace memory
    {
        //
    }
} // namespace m
