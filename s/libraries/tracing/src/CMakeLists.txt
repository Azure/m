cmake_minimum_required(VERSION 3.23)

target_sources(m_tracing PRIVATE
    channel.cpp
    cout_sink.cpp
    envelope.cpp
    event_context.cpp
    message.cpp
    message_queue.cpp
    monitor_class.cpp
    multiplexor.cpp
    sink.cpp
    source.cpp
  )

target_include_directories(m_tracing PUBLIC
    ../include
)

target_link_libraries(m_tracing PUBLIC
    m_math
    m_strings
    m_utf
)
