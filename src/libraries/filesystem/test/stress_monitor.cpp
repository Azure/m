// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <m/debugging/dbg_format.h>
#include <m/filesystem/filesystem.h>
#include <m/googletest/temporary_directory.h>
#include <m/threadpool/threadpool.h>

using namespace std::chrono_literals;
using namespace std::string_literals;
using namespace std::string_view_literals;

struct change_notification_sink : public m::filesystem::change_notification
{
    std::atomic<uintmax_t> m_on_begin_count{0};
    std::atomic<uintmax_t> m_on_directory_access_failure_count{0};
    std::atomic<uintmax_t> m_on_file_access_failure_count{0};
    std::atomic<uintmax_t> m_on_file_changed_count{0};
    std::atomic<uintmax_t> m_on_file_deleted_count{0};
    std::atomic<uintmax_t> m_on_file_recheck_required_count{0};
    std::atomic<uintmax_t> m_on_cancelled_count{0};
    std::atomic<uintmax_t> m_on_invalid_count{0};

    void
    on_begin() override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_begin_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    std::optional<requeue_directory_access_attempt>
    on_directory_access_failure(std::filesystem::path const&             directory,
                                std::filesystem::filesystem_error const& error) override
    {
        m::dbg_format("Entering {}", __func__);
        std::ignore = directory;
        std::ignore = error;

        m_on_directory_access_failure_count.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    std::optional<requeue_file_access_attempt>
    on_file_access_failure(std::filesystem::path const&             directory,
                           std::filesystem::path const&             file,
                           std::filesystem::filesystem_error const& error) override
    {
        m::dbg_format("Entering {}", __func__);
        std::ignore = directory;
        std::ignore = file;
        std::ignore = error;

        m_on_directory_access_failure_count.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    void
    on_file_changed(std::filesystem::path const&, std::filesystem::path const&) override
    {
        // m::dbg_format("Entering {}", __func__);
        m_on_file_changed_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_file_deleted(std::filesystem::path const&, std::filesystem::path const&) override
    {
        // m::dbg_format("Entering {}", __func__);
        m_on_file_deleted_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_file_recheck_required(std::filesystem::path const&, std::filesystem::path const&) override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_file_recheck_required_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_cancelled() override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_cancelled_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_invalid() override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_invalid_count.fetch_add(1, std::memory_order_relaxed);
        //
    }
};

//
// Implement std::formatter<> for these types so that we can compose these things together
// more simply
//

template <typename CharT>
struct std::formatter<change_notification_sink, CharT>
{
    using thetype = change_notification_sink;

    template <typename ParseContext>
    constexpr auto
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(thetype const& x, FormatContext& ctx) const
    {
        auto it = ctx.out();

        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            it = std::format_to(
                it,
                L"{{ m_on_begin_count: {}, m_on_directory_access_failure_count: {}, m_on_file_access_failure_count: {}, m_on_file_changed_count: {}, m_on_file_deleted_count: {}, m_on_file_recheck_required_count: {}, m_on_cancelled_count: {}, m_on_invalid_count: {} }}",
                x.m_on_begin_count.load(std::memory_order_relaxed),
                x.m_on_directory_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_changed_count.load(std::memory_order_relaxed),
                x.m_on_file_deleted_count.load(std::memory_order_relaxed),
                x.m_on_file_recheck_required_count.load(std::memory_order_relaxed),
                x.m_on_cancelled_count.load(std::memory_order_relaxed),
                x.m_on_invalid_count.load(std::memory_order_relaxed));
        }
        else if constexpr (std::is_same_v<CharT, char>)
        {
            it = std::format_to(
                it,
                "{{ m_on_begin_count: {}, m_on_directory_access_failure_count: {}, m_on_file_access_failure_count: {}, m_on_file_changed_count: {}, m_on_file_deleted_count: {}, m_on_file_recheck_required_count: {}, m_on_cancelled_count: {}, m_on_invalid_count: {} }}",
                x.m_on_begin_count.load(std::memory_order_relaxed),
                x.m_on_directory_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_changed_count.load(std::memory_order_relaxed),
                x.m_on_file_deleted_count.load(std::memory_order_relaxed),
                x.m_on_file_recheck_required_count.load(std::memory_order_relaxed),
                x.m_on_cancelled_count.load(std::memory_order_relaxed),
                x.m_on_invalid_count.load(std::memory_order_relaxed));
        }
        else
        {
            throw std::runtime_error("Unexpected CharT");
        }

        return it;
    }
};

struct test_file_contents
{
    test_file_contents(std::u8string_view sv):
        m_contents(sv), m_span(m_contents.begin(), m_contents.end()), m_bytes(std::as_bytes(m_span))
    {}

    constexpr
    operator std::span<std::byte const>() const
    {
        return m_bytes;
    }

    void
    store_into(std::filesystem::path const& p) const
    {
        m::filesystem::store(p, m_bytes);
    }

    // Might make sense to call this "try_store_into" but the patterm
    // from filesystem is that when std::error_code& is passed not to
    // return bool and not to call it "try_" so we don't do it here
    // either.
    void
    store_into(std::filesystem::path const& p, std::error_code ec) const
    {
        m::filesystem::store(p, m_bytes, ec);
    }

    std::u8string_view         m_contents;
    std::span<char8_t const>   m_span;
    std::span<std::byte const> m_bytes;
};

auto f1 = test_file_contents(u8"Contents1\n"sv);
auto f2 = test_file_contents(u8"Contents2\n"sv);

struct file_harness_core_data
{
    file_harness_core_data(std::filesystem::path const& path): m_path(path) {}

    std::filesystem::path const& m_path;
    std::atomic<uintmax_t>       m_attempts{0};
    std::atomic<uintmax_t>       m_failures{0};
    std::atomic<uintmax_t>       m_successes{0};
};

template <typename CharT>
struct std::formatter<file_harness_core_data, CharT>
{
    using thetype = file_harness_core_data;

    template <typename ParseContext>
    constexpr auto
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(thetype const& x, FormatContext& ctx) const
    {
        auto it = ctx.out();

        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            auto wstr = m::to_wstring(x.m_path.native());
            it        = std::format_to(it,
                                L"{{ {}: m_attempts: {}, m_failures: {}, m_successes: {} }}",
                                wstr,
                                x.m_attempts.load(std::memory_order_relaxed),
                                x.m_failures.load(std::memory_order_relaxed),
                                x.m_successes.load(std::memory_order_relaxed));
        }
        else if constexpr (std::is_same_v<CharT, char>)
        {
            auto str = m::to_string(x.m_path.native());
            it       = std::format_to(it,
                                "{{ {}: m_attempts: {}, m_failures: {}, m_successes: {} }}",
                                str,
                                x.m_attempts.load(std::memory_order_relaxed),
                                x.m_failures.load(std::memory_order_relaxed),
                                x.m_successes.load(std::memory_order_relaxed));
        }
        else
        {
            throw std::runtime_error("Unexpected CharT");
        }

        return it;
    }
};

struct store_file_harness
{
    store_file_harness(std::filesystem::path const& path, test_file_contents const& contents):
        m_core(path), m_contents(contents)
    {}

    bool
    try_store()
    {
        m_core.m_attempts.fetch_add(1, std::memory_order_relaxed);
        std::error_code ec;
        m_contents.store_into(m_core.m_path, ec);
        if (ec)
        {
            m_core.m_failures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        m_core.m_successes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    file_harness_core_data    m_core;
    test_file_contents const& m_contents;
};

template <typename CharT>
struct std::formatter<store_file_harness, CharT>
{
    using thetype = store_file_harness;

    template <typename ParseContext>
    constexpr auto
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(thetype const& x, FormatContext& ctx) const
    {
        auto it = ctx.out();

        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            it = std::format_to(it, L"{{ store {} }}", x.m_core);
        }
        else if constexpr (std::is_same_v<CharT, char>)
        {
            it = std::format_to(it, "{{ store {} }}", x.m_core);
        }
        else
        {
            throw std::runtime_error("Unexpected CharT");
        }

        return it;
    }
};

struct remove_file_harness
{
    remove_file_harness(std::filesystem::path const& path): m_core(path) {}

    bool
    try_remove()
    {
        m_core.m_attempts.fetch_add(1, std::memory_order_relaxed);

        std::error_code ec;
        std::filesystem::remove(m_core.m_path, ec);

        if (ec)
        {
            m_core.m_failures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        m_core.m_successes.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    file_harness_core_data m_core;
};

template <typename CharT>
struct std::formatter<remove_file_harness, CharT>
{
    using thetype = remove_file_harness;

    template <typename ParseContext>
    constexpr auto
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(thetype const& x, FormatContext& ctx) const
    {
        auto it = ctx.out();

        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            it = std::format_to(it, L"{{ remove {} }}", x.m_core);
        }
        else if constexpr (std::is_same_v<CharT, char>)
        {
            it = std::format_to(it, "{{ remove {} }}", x.m_core);
        }
        else
        {
            throw std::runtime_error("Unexpected CharT");
        }

        return it;
    }
};

class icao_uniform_int_distribution
{
    // Returns values in the range [0 .. 35] where the expectation is that
    // 0 .. 9 -> "Zero" .. "Nine", and 10 .. 35 -> "Alfa" .. "Zulu"
    constexpr static inline uint8_t value_a = 0;
    constexpr static inline uint8_t value_b = 35;

public:
    using result_type = uint16_t;

    icao_uniform_int_distribution(): m_dist(value_a, value_b) {}
    icao_uniform_int_distribution(icao_uniform_int_distribution const& other): m_dist(other.m_dist)
    {}
    icao_uniform_int_distribution(icao_uniform_int_distribution&& other)
    {
        using std::swap;
        swap(m_dist, other.m_dist);
    }
    ~icao_uniform_int_distribution() = default;
    icao_uniform_int_distribution&
    operator=(icao_uniform_int_distribution& other)
    {
        m_dist = other.m_dist;
        return *this;
    }
    icao_uniform_int_distribution&
    operator=(icao_uniform_int_distribution&& other)
    {
        using std::swap;
        swap(m_dist, other.m_dist);
        return *this;
    }

    template <typename Generator>
    result_type
    operator()(Generator& g)
    {
        return m_dist(g);
    }

    void
    reset()
    {
        m_dist.reset();
    }

    result_type
    a() const
    {
        return value_a;
    }

    result_type
    b() const
    {
        return value_b;
    }

    result_type
    min() const
    {
        return value_a;
    }

    result_type
    max() const
    {
        return value_b;
    }

    // The icao_uniform_int_distribution type does not quite meet the
    // definition of RandomNumberDistribution because it does not
    // implement the param_type aspects. It's unclear what to do with
    // them.

private:
    std::uniform_int_distribution<std::uint16_t> m_dist;
};

class icao_letter_generator
{
public:
    icao_letter_generator() {}

    template <typename Generator>
    std::wstring
    operator()(Generator& g)
    {
        auto n = m_dist(g);

        constexpr std::array<std::wstring_view, 36> letters = {
            L"zero"sv,
            L"one"sv,
            L"two"sv
            L"three"sv,
            L"four"sv,
            L"five"sv,
            L"six"sv,
            L"seven"sv,
            L"eight"sv,
            L"nine"sv,
            L"alfa"sv,
            L"bravo"sv,
            L"charlie"sv,
            L"delta"sv,
            L"echo"sv,
            L"foxtrot"sv,
            L"golf"sv,
            L"hotel"sv,
            L"india"sv,
            L"juliett"sv,
            L"kilo"sv,
            L"lima"sv,
            L"mike"sv,
            L"november"sv,
            L"oscar"sv,
            L"papa"sv,
            L"quebec"sv,
            L"romeo"sv,
            L"sierra"sv,
            L"tango"sv,
            L"uniform"sv,
            L"victor"sv,
            L"whiskey"sv,
            L"xray"sv,
            L"yankee"sv,
            L"zulu"sv,
        };

        return std::wstring(letters[n]);
    }

private:
    icao_uniform_int_distribution m_dist;
};

class random_filename_generator
{
public:
    random_filename_generator(): m_length_dist(3, 10) {}

    template <typename Generator>
    std::wstring
    operator()(Generator& g)
    {
        auto length = m_length_dist(g);

        std::wstring result;
        result.reserve(
            length *
            8); // 8 chars for each ICAO "letter" seems like a good starting guess to avoid resizing

        while (length != 0)
        {
            // If this isn't the first run through the loop, affix an underscore to the
            // string to separate the "letters".
            if (result.size() != 0)
                result = result + L"_";

            result = result + m_letter_gen(g);

            length--;
        }

        return result;
    }

protected:
    std::uniform_int_distribution<uint16_t>
        m_length_dist; // this is in terms of the ICAO letters so 3-10 is not small
    icao_letter_generator m_letter_gen;
};

TEST(StressMonitor, test)
{
    auto const td      = m::googletest::create_temporary_directory();
    auto const td_path = td->path();

    auto const p = m::filesystem::make_path(td_path, L"temporary_file");

    // bad_p is, in theory, underneath p. But if p is a file, monitoring
    // bad_p is going to have a bad time.
    auto const bad_p = m::filesystem::make_path(p, L"another_file");

    f1.store_into(p);

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;
    change_notification_sink cns_bad_p;

    std::atomic<bool> stop_indicator{false};
    auto              testf1     = store_file_harness(p, f1);
    auto              testf2     = store_file_harness(p, f2);
    auto              testremove = remove_file_harness(p);

    auto store_contents_1_l = [&]() {
        while (!stop_indicator.load())
        {
            testf1.try_store();
        }
    };

    auto store_contents_2_l = [&]() {
        while (!stop_indicator.load())
        {
            testf2.try_store();
        }
    };

    auto remove_l = [&]() {
        while (!stop_indicator.load())
        {
            testremove.try_remove();
        }
    };

    // thread that will generate some number of randomly named
    // files in the temp dir, and then delete them, over and over
    auto random_creater_then_deleter_l = [&]() {
        std::random_device    rd;
        auto                  gen                    = std::mt19937_64(rd());
        constexpr std::size_t file_count_lower_limit = 5;
        constexpr std::size_t file_count_upper_limit = 20;
        auto                  count_distribution     = std::uniform_int_distribution<std::size_t>(
            file_count_lower_limit,
            file_count_upper_limit); // on each iteration we will create / delete a set of files
                                     // that number between file_count_lower_limit and
                                     // file_count_upper_limit.
        std::vector<std::basic_string<typename std::filesystem::path::value_type>> names;
        std::vector<std::filesystem::path>                                         paths;
        random_filename_generator                                                  rfg;

        names.reserve(file_count_upper_limit);
        paths.reserve(file_count_upper_limit);

        while (!stop_indicator.load())
        {
            auto const file_count = count_distribution(gen);
            names.resize(file_count);
            paths.resize(file_count);

            for (auto&& n: names)
                n = m::filesystem::to_native(rfg(gen));

            for (size_t i = 0; i < names.size(); i++)
                paths[i] = m::filesystem::make_path(td_path, names[i]);

            // Could have stored the files in the same loop but whatever
            for (auto&& p: paths)
            {
                std::error_code ec;
                f1.store_into(p, ec);
            }

            for (auto&& p: paths)
            {
                std::error_code ec;
                std::filesystem::remove(p, ec);
            }
        }
    };

    auto start1 = std::chrono::system_clock::now();

    {
        auto const token  = monitor->register_watch(p, &cns);
        auto const token2 = monitor->register_watch(bad_p, &cns_bad_p);

        auto t1 = std::thread(store_contents_1_l);
        auto t2 = std::thread(store_contents_2_l);
        auto t3 = std::thread(remove_l);
        auto t4 = std::thread(random_creater_then_deleter_l);

        std::this_thread::sleep_for(20s);

        stop_indicator.store(true);

        t1.join();
        t2.join();
        t3.join();

        auto end1      = std::chrono::system_clock::now();
        auto delta1    = end1 - start1;
        auto delta1_ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta1);

        std::println(
            "After {}ms\n"
            "   Contents 1: {}\n"
            "   Contents 2: {}\n"
            "   Removes: {}\n"
            "   cns: {}\n"
            "   cns_bad_p: {}\n",
            delta1_ms.count(),
            testf1,
            testf2,
            testremove,
            cns,
            cns_bad_p);
    }
}
