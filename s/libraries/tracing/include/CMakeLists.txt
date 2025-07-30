cmake_minimum_required(VERSION 3.23)

target_sources(m_tracing PUBLIC FILE_SET HEADERS FILES 
    m/tracing/channel.h 
    m/tracing/cout_sink.h 
    m/tracing/envelope.h 
    m/tracing/event_context.h 
    m/tracing/event_kind.h 
    m/tracing/message.h 
    m/tracing/message_queue.h 
    m/tracing/monitor_class.h 
    m/tracing/monitor_class_var.h 
    m/tracing/multiplexor.h 
    m/tracing/on_message_disposition.h 
    m/tracing/safe_array_iterator.h 
    m/tracing/sink.h 
    m/tracing/source.h 
    m/tracing/topology_version.h 
    m/tracing/tracing.h 
)
