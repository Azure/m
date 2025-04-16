cmake_minimum_required(VERSION 3.30)

add_library(tracing STATIC
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

target_include_directories(tracing PUBLIC
    ../include
)

target_link_libraries(tracing PUBLIC
    math
    strings
    utf
)

set_target_properties(tracing PROPERTIES
    VERSION ${VERSION}
)

