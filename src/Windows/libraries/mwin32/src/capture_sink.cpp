// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "capture_sink.h"

#include <utility>

namespace m::mwin32_impl
{
    void tallying_capture_sink::on_request(http_request const& request)
    {
        ++m_request_count;
        ++m_by_method[request.method];
    }

    void tallying_capture_sink::on_response(http_response const& response)
    {
        ++m_response_count;
        ++m_by_status[response.status_code];
    }

    void tallying_capture_sink::on_crossing(http_crossing const& /*crossing*/)
    {
        ++m_crossing_count;
    }

    std::size_t tallying_capture_sink::requests_with_method(
        std::string const& method) const
    {
        auto const it = m_by_method.find(method);
        return it == m_by_method.end() ? 0 : it->second;
    }

    std::size_t tallying_capture_sink::responses_with_status(
        int status_code) const
    {
        auto const it = m_by_status.find(status_code);
        return it == m_by_status.end() ? 0 : it->second;
    }

    void connection_capture::on_request_bytes(void const* data,
                                              std::size_t length)
    {
        m_request_stream.feed(data, length);
        drain();
    }

    void connection_capture::on_response_bytes(void const* data,
                                               std::size_t length)
    {
        m_response_stream.feed(data, length);
        drain();
    }

    void connection_capture::drain()
    {
        http_request request;
        while (m_request_stream.next(request))
        {
            m_sink.on_request(request);
            m_unpaired_requests.push_back(std::move(request));
        }

        http_response response;
        while (m_response_stream.next(response))
        {
            m_sink.on_response(response);
            m_unpaired_responses.push_back(std::move(response));
        }

        while (!m_unpaired_requests.empty() && !m_unpaired_responses.empty())
        {
            http_crossing crossing;
            crossing.request = std::move(m_unpaired_requests.front());
            crossing.response = std::move(m_unpaired_responses.front());
            m_unpaired_requests.pop_front();
            m_unpaired_responses.pop_front();
            m_sink.on_crossing(crossing);
        }
    }
} // namespace m::mwin32_impl
