// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// M-HWC-ENGINE-EDGE-4: tests for the in-process webcore engine
// (make_in_process_webcore). The engine activates a real in-process HTTP edge
// serviced by a caller-supplied handler — the deterministic, IIS-free engine
// the synthetic edge (D-HWC-6 Tier B) was designed for. These tests drive it
// through the public submit/observe seam (D-HWC-11).
//

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <m/pil/file_path.h>
#include <m/pil/http_contract_interfaces.h>
#include <m/pil/in_process_webcore.h>
#include <m/pil/synthetic_http_edge.h>
#include <m/pil/webcore_interfaces.h>

namespace
{
    using m::pil::activation_request;
    using m::pil::captured_contract_response;
    using m::pil::crossing_observer;
    using m::pil::engine_submit;
    using m::pil::file_path;
    using m::pil::isynthetic_http_edge;
    using m::pil::iwebcore;
    using m::pil::iwebcore_instance;
    using m::pil::make_engine_submit;
    using m::pil::make_in_process_webcore;
    using m::pil::null_webcore_instance;
    using m::pil::synthesized_request;
    using m::pil::synthetic_request_handler;

    constexpr std::chrono::milliseconds k_timeout{4000};

    // A minimal activation request (the in-process engine ignores its fields).
    activation_request
    make_request()
    {
        activation_request request;
        request.app_host_config = file_path(u"C:\\test\\applicationHost.config");
        request.instance_name   = u"InProcess";
        return request;
    }

    // Build a synthesized request with a method and path (no body).
    synthesized_request
    request_for(std::string method, std::string path)
    {
        synthesized_request req;
        req.method = std::move(method);
        req.path   = std::move(path);
        return req;
    }

    std::vector<std::uint8_t>
    bytes(std::string_view s)
    {
        return std::vector<std::uint8_t>(reinterpret_cast<std::uint8_t const*>(s.data()),
                                         reinterpret_cast<std::uint8_t const*>(s.data()) + s.size());
    }

    // 1. Activation yields a non-null edge.
    TEST(InProcessWebcore, ActivateYieldsEdge)
    {
        auto engine = make_in_process_webcore(
            [](synthesized_request const&) { return captured_contract_response{}; });

        std::unique_ptr<iwebcore_instance> instance = engine->activate(make_request());
        ASSERT_NE(instance, nullptr);
        EXPECT_NE(instance->synthetic_http_edge(), nullptr);
    }

    // 2. A conforming (200) response round-trips through submit.
    TEST(InProcessWebcore, SubmitConformingResponse)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status = 200;
            resp.body   = bytes("ok");
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        auto response = edge->submit(request_for("GET", "/widgets"), k_timeout);
        EXPECT_EQ(response.status, 200);
        EXPECT_EQ(response.body, bytes("ok"));
    }

    // 3. A violating (500) response round-trips equally (the engine does not
    //    validate — it returns whatever the handler produced).
    TEST(InProcessWebcore, SubmitViolatingResponse)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status = 500;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        auto response = edge->submit(request_for("POST", "/widgets"), k_timeout);
        EXPECT_EQ(response.status, 500);
    }

    // 4. The handler responds based on request path; multiple sequential submits
    //    each get the right response.
    TEST(InProcessWebcore, HandlerRoutesByPath)
    {
        auto engine = make_in_process_webcore([](synthesized_request const& req) {
            captured_contract_response resp;
            resp.status = (req.path == "/ok") ? 200 : 404;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        EXPECT_EQ(edge->submit(request_for("GET", "/ok"), k_timeout).status, 200);
        EXPECT_EQ(edge->submit(request_for("GET", "/missing"), k_timeout).status, 404);
        EXPECT_EQ(edge->submit(request_for("GET", "/ok"), k_timeout).status, 200);
    }

    // 5. The handler sees the submitted request fields (method, path, body).
    TEST(InProcessWebcore, HandlerReceivesRequestFields)
    {
        std::mutex                  mutex;
        std::optional<std::string>  seen_method;
        std::optional<std::string>  seen_path;
        std::optional<std::vector<std::uint8_t>> seen_body;

        auto engine = make_in_process_webcore([&](synthesized_request const& req) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                seen_method = req.method;
                seen_path   = req.path;
                seen_body   = req.body;
            }
            captured_contract_response resp;
            resp.status = 201;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        synthesized_request req = request_for("PUT", "/widgets/7");
        req.body                = bytes("payload");
        auto response           = edge->submit(req, k_timeout);

        EXPECT_EQ(response.status, 201);
        std::lock_guard<std::mutex> guard(mutex);
        EXPECT_EQ(seen_method.value(), "PUT");
        EXPECT_EQ(seen_path.value(), "/widgets/7");
        EXPECT_EQ(seen_body.value(), bytes("payload"));
    }

    // 6. submit composes into an engine_submit via make_engine_submit (the form
    //    drive_contract consumes).
    TEST(InProcessWebcore, MakeEngineSubmitDrivesEngine)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status = 200;
            resp.body   = bytes("driven");
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        engine_submit submit = make_engine_submit(edge, k_timeout);
        ASSERT_TRUE(static_cast<bool>(submit));

        auto response = submit(request_for("GET", "/widgets"));
        EXPECT_EQ(response.status, 200);
        EXPECT_EQ(response.body, bytes("driven"));
    }

    // 7. A registered crossing observer sees the serviced crossing.
    TEST(InProcessWebcore, ObserverSeesCrossing)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status = 200;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        std::mutex                   mutex;
        int                          observed = 0;
        std::optional<std::string>   obs_path;
        std::optional<std::uint16_t> obs_status;
        edge->add_crossing_observer(
            [&](synthesized_request const& req, captured_contract_response const& resp) {
                std::lock_guard<std::mutex> guard(mutex);
                ++observed;
                obs_path   = req.path;
                obs_status = resp.status;
            });

        edge->submit(request_for("GET", "/tapped"), k_timeout);

        std::lock_guard<std::mutex> guard(mutex);
        EXPECT_EQ(observed, 1);
        EXPECT_EQ(obs_path.value(), "/tapped");
        EXPECT_EQ(obs_status.value(), 200);
    }

    // 8. The observer sees every crossing across multiple submits.
    TEST(InProcessWebcore, ObserverSeesEveryCrossing)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status = 200;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        std::mutex mutex;
        int        observed = 0;
        edge->add_crossing_observer(
            [&](synthesized_request const&, captured_contract_response const&) {
                std::lock_guard<std::mutex> guard(mutex);
                ++observed;
            });

        constexpr int count = 5;
        for (int i = 0; i < count; ++i)
            edge->submit(request_for("GET", "/n"), k_timeout);

        std::lock_guard<std::mutex> guard(mutex);
        EXPECT_EQ(observed, count);
    }

    // 9. Response headers are preserved through the round trip.
    TEST(InProcessWebcore, ResponseHeadersPreserved)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status  = 200;
            resp.headers = {{"Content-Type", "application/json"}, {"X-Trace", "abc"}};
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        auto response = edge->submit(request_for("GET", "/widgets"), k_timeout);
        ASSERT_EQ(response.headers.size(), 2u);
        EXPECT_EQ(response.headers[0].first, "Content-Type");
        EXPECT_EQ(response.headers[0].second, "application/json");
        EXPECT_EQ(response.headers[1].first, "X-Trace");
        EXPECT_EQ(response.headers[1].second, "abc");
    }

    // 10. A null_webcore_instance yields no edge, and make_engine_submit over a
    //     null edge is a null engine_submit.
    TEST(InProcessWebcore, NullInstanceYieldsNoEdge)
    {
        null_webcore_instance instance;
        iwebcore_instance&    as_iface = instance;

        EXPECT_EQ(as_iface.synthetic_http_edge(), nullptr);

        engine_submit submit = make_engine_submit(as_iface.synthetic_http_edge(), k_timeout);
        EXPECT_FALSE(static_cast<bool>(submit));
    }

    // 11. Concurrent submits from several threads each get a response.
    TEST(InProcessWebcore, ConcurrentSubmits)
    {
        auto engine = make_in_process_webcore([](synthesized_request const& req) {
            captured_contract_response resp;
            resp.status = 200;
            resp.body   = bytes(req.path);
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        constexpr int        thread_count = 8;
        std::atomic<int>     successes{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < thread_count; ++i)
        {
            threads.emplace_back([edge, i, &successes] {
                std::string path     = "/n" + std::to_string(i);
                auto        response = edge->submit(request_for("GET", path), k_timeout);
                if (response.status == 200 && response.body == bytes(path))
                    ++successes;
            });
        }
        for (auto& t : threads)
            t.join();

        EXPECT_EQ(successes.load(), thread_count);
    }

    // 12. Destroying the engine instance with no in-flight work shuts the worker
    //     down cleanly (no hang, no terminate).
    TEST(InProcessWebcore, CleanShutdown)
    {
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            captured_contract_response resp;
            resp.status = 200;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        edge->submit(request_for("GET", "/widgets"), k_timeout);

        // Releasing the instance joins the worker thread; reaching here proves a
        // clean shutdown.
        instance.reset();
        SUCCEED();
    }

    // 13. A submit whose handler is never going to answer times out and yields a
    //     default (status 0) response rather than blocking forever.
    TEST(InProcessWebcore, SubmitTimesOutToDefault)
    {
        // The handler blocks briefly so the first submit's short timeout elapses.
        auto engine = make_in_process_webcore([](synthesized_request const&) {
            std::this_thread::sleep_for(std::chrono::milliseconds{60});
            captured_contract_response resp;
            resp.status = 200;
            return resp;
        });

        auto instance = engine->activate(make_request());
        auto* edge    = instance->synthetic_http_edge();
        ASSERT_NE(edge, nullptr);

        auto response = edge->submit(request_for("GET", "/slow"), std::chrono::milliseconds{1});
        EXPECT_EQ(response.status, 0);
    }
} // namespace
