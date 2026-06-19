// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "http_reassembler.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

using m::mwin32_impl::http_request;
using m::mwin32_impl::http_request_reassembler;
using m::mwin32_impl::http_response;
using m::mwin32_impl::http_response_reassembler;

namespace
{
    // Feed a string literal chunk into a reassembler.
    template <typename Reassembler>
    void feed(Reassembler& r, std::string_view chunk)
    {
        r.feed(chunk.data(), chunk.size());
    }

    std::string body_string(std::vector<std::uint8_t> const& body)
    {
        return std::string(body.begin(), body.end());
    }

    // Find a header value by case-insensitive name; empty if absent.
    template <typename Message>
    std::string header_value(Message const& m, std::string_view name)
    {
        for (auto const& h : m.headers)
        {
            if (h.name.size() != name.size())
            {
                continue;
            }
            bool equal = true;
            for (std::size_t i = 0; i < name.size(); ++i)
            {
                char const a =
                    static_cast<char>(std::tolower(
                        static_cast<unsigned char>(h.name[i])));
                char const b =
                    static_cast<char>(std::tolower(
                        static_cast<unsigned char>(name[i])));
                if (a != b)
                {
                    equal = false;
                    break;
                }
            }
            if (equal)
            {
                return h.value;
            }
        }
        return std::string();
    }
} // namespace

// --- Request reassembly --------------------------------------------------

TEST(HttpRequestReassembler, SimpleGetNoBody)
{
    http_request_reassembler r;
    feed(r, "GET /index.html HTTP/1.1\r\nHost: example.com\r\n\r\n");

    http_request req;
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.target, "/index.html");
    EXPECT_EQ(req.version, "HTTP/1.1");
    EXPECT_EQ(header_value(req, "host"), "example.com");
    EXPECT_TRUE(req.body.empty());
    EXPECT_FALSE(r.next(req));
}

TEST(HttpRequestReassembler, PostWithContentLengthBody)
{
    http_request_reassembler r;
    feed(r,
         "POST /api/items HTTP/1.1\r\n"
         "Host: example.com\r\n"
         "Content-Type: application/json\r\n"
         "Content-Length: 13\r\n"
         "\r\n"
         "{\"name\":\"x\"}\n");

    http_request req;
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.target, "/api/items");
    EXPECT_EQ(body_string(req.body), "{\"name\":\"x\"}\n");
    EXPECT_EQ(req.body.size(), 13u);
    EXPECT_FALSE(r.next(req));
}

TEST(HttpRequestReassembler, ZeroContentLengthIsEmptyBody)
{
    http_request_reassembler r;
    feed(r,
         "POST /ping HTTP/1.1\r\n"
         "Content-Length: 0\r\n"
         "\r\n");

    http_request req;
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(req.method, "POST");
    EXPECT_TRUE(req.body.empty());
}

TEST(HttpRequestReassembler, MissingContentLengthIsEmptyBody)
{
    // No Content-Length and no chunked: v1 treats the body as empty and the
    // bytes after the blank line belong to the next message.
    http_request_reassembler r;
    feed(r,
         "GET /a HTTP/1.1\r\n"
         "Host: h\r\n"
         "\r\n"
         "GET /b HTTP/1.1\r\n"
         "Host: h\r\n"
         "\r\n");

    http_request first;
    ASSERT_TRUE(r.next(first));
    EXPECT_EQ(first.target, "/a");
    EXPECT_TRUE(first.body.empty());

    http_request second;
    ASSERT_TRUE(r.next(second));
    EXPECT_EQ(second.target, "/b");
    EXPECT_FALSE(r.next(second));
}

TEST(HttpRequestReassembler, SplitAcrossManyFeeds)
{
    // The message arrives one or two bytes at a time; nothing completes until
    // the final body byte lands.
    http_request_reassembler r;
    std::string const wire =
        "PUT /x HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";

    http_request req;
    for (std::size_t i = 0; i + 1 < wire.size(); ++i)
    {
        feed(r, std::string_view(&wire[i], 1));
        EXPECT_FALSE(r.next(req)) << "completed early at byte " << i;
    }
    feed(r, std::string_view(&wire[wire.size() - 1], 1));
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(req.method, "PUT");
    EXPECT_EQ(body_string(req.body), "hello");
}

TEST(HttpRequestReassembler, HeaderBlockSplitMidHeader)
{
    http_request_reassembler r;
    feed(r, "GET /split HTTP/1.1\r\nHost: ex");
    http_request req;
    EXPECT_FALSE(r.next(req));
    feed(r, "ample.com\r\n\r\n");
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(req.target, "/split");
    EXPECT_EQ(header_value(req, "Host"), "example.com");
}

TEST(HttpRequestReassembler, PipelinedKeepAlive)
{
    http_request_reassembler r;
    feed(r,
         "POST /one HTTP/1.1\r\nContent-Length: 3\r\n\r\nAAA"
         "GET /two HTTP/1.1\r\nContent-Length: 0\r\n\r\n"
         "POST /three HTTP/1.1\r\nContent-Length: 2\r\n\r\nZZ");

    http_request a;
    ASSERT_TRUE(r.next(a));
    EXPECT_EQ(a.target, "/one");
    EXPECT_EQ(body_string(a.body), "AAA");

    http_request b;
    ASSERT_TRUE(r.next(b));
    EXPECT_EQ(b.target, "/two");
    EXPECT_TRUE(b.body.empty());

    http_request c;
    ASSERT_TRUE(r.next(c));
    EXPECT_EQ(c.target, "/three");
    EXPECT_EQ(body_string(c.body), "ZZ");

    EXPECT_FALSE(r.next(a));
}

TEST(HttpRequestReassembler, ContentLengthHeaderNameIsCaseInsensitive)
{
    http_request_reassembler r;
    feed(r,
         "POST /ci HTTP/1.1\r\n"
         "content-LENGTH: 4\r\n"
         "\r\n"
         "data");

    http_request req;
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(body_string(req.body), "data");
}

TEST(HttpRequestReassembler, HeaderValueWhitespaceIsTrimmed)
{
    http_request_reassembler r;
    feed(r,
         "GET /ws HTTP/1.1\r\n"
         "X-Custom: \t  spaced value  \t\r\n"
         "\r\n");

    http_request req;
    ASSERT_TRUE(r.next(req));
    EXPECT_EQ(header_value(req, "X-Custom"), "spaced value");
}

TEST(HttpRequestReassembler, BodyWithBinaryAndEmbeddedCrlf)
{
    // The body is opaque bytes: NUL and CRLF inside it must not confuse the
    // framing, which is length-delimited.
    http_request_reassembler r;
    std::string body;
    body.push_back('\0');
    body += "\r\n\r\nmid";
    body.push_back('\xff');
    std::string wire =
        "POST /bin HTTP/1.1\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;

    feed(r, wire);
    http_request req;
    ASSERT_TRUE(r.next(req));
    ASSERT_EQ(req.body.size(), body.size());
    EXPECT_EQ(body_string(req.body), body);
    EXPECT_FALSE(r.next(req));
}

// --- Response reassembly -------------------------------------------------

TEST(HttpResponseReassembler, SimpleOkWithBody)
{
    http_response_reassembler r;
    feed(r,
         "HTTP/1.1 200 OK\r\n"
         "Content-Type: text/plain\r\n"
         "Content-Length: 5\r\n"
         "\r\n"
         "hello");

    http_response res;
    ASSERT_TRUE(r.next(res));
    EXPECT_EQ(res.version, "HTTP/1.1");
    EXPECT_EQ(res.status_code, 200);
    EXPECT_EQ(res.reason, "OK");
    EXPECT_EQ(body_string(res.body), "hello");
    EXPECT_FALSE(r.next(res));
}

TEST(HttpResponseReassembler, MultiWordReasonPhrase)
{
    http_response_reassembler r;
    feed(r,
         "HTTP/1.1 404 Not Found\r\n"
         "Content-Length: 0\r\n"
         "\r\n");

    http_response res;
    ASSERT_TRUE(r.next(res));
    EXPECT_EQ(res.status_code, 404);
    EXPECT_EQ(res.reason, "Not Found");
    EXPECT_TRUE(res.body.empty());
}

TEST(HttpResponseReassembler, NoReasonPhrase)
{
    http_response_reassembler r;
    feed(r,
         "HTTP/1.1 204\r\n"
         "Content-Length: 0\r\n"
         "\r\n");

    http_response res;
    ASSERT_TRUE(r.next(res));
    EXPECT_EQ(res.status_code, 204);
    EXPECT_EQ(res.reason, "");
}

TEST(HttpResponseReassembler, PipelinedResponses)
{
    http_response_reassembler r;
    feed(r,
         "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"
         "HTTP/1.1 500 Server Error\r\nContent-Length: 3\r\n\r\nbad");

    http_response a;
    ASSERT_TRUE(r.next(a));
    EXPECT_EQ(a.status_code, 200);
    EXPECT_EQ(body_string(a.body), "hi");

    http_response b;
    ASSERT_TRUE(r.next(b));
    EXPECT_EQ(b.status_code, 500);
    EXPECT_EQ(b.reason, "Server Error");
    EXPECT_EQ(body_string(b.body), "bad");

    EXPECT_FALSE(r.next(a));
}

TEST(HttpResponseReassembler, SplitResponseBody)
{
    http_response_reassembler r;
    feed(r, "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nab");
    http_response res;
    EXPECT_FALSE(r.next(res));
    feed(r, "cd");
    ASSERT_TRUE(r.next(res));
    EXPECT_EQ(body_string(res.body), "abcd");
}
