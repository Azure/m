cmake_minimum_required(VERSION 3.23)

target_sources(m_tracing PRIVATE
    channel.cpp
    cout_sink.cpp
    envelope.cpp
    event_context.cpp
    helper_functions.cpp
    message.cpp
    message_allocator.cpp
    message_queue.cpp
    message_source.cpp
    monitor_class_impl.cpp
    multiplexor_impl.cpp
    sink.cpp
    sink_registration_impl.cpp
    sink_shim.cpp
    source.cpp
  )

target_include_directories(m_tracing PUBLIC
    ../include
)

target_link_libraries(m_tracing PUBLIC
    m_atomic
    m_chrono
    m_debugging
    m_error_handling
    m_math
    m_print
    m_strings
    m_utf
)
