cmake_minimum_required(VERSION 3.23)

target_sources(m_tracing PUBLIC FILE_SET HEADERS FILES
    m/tracing/channel.h
    m/tracing/close_flush_option.h
    m/tracing/cout_sink.h 
    m/tracing/debugging.h
    m/tracing/envelope.h 
    m/tracing/event_context.h 
    m/tracing/event_kind.h
    m/tracing/format_view.h
    m/tracing/frame.h
    m/tracing/imessage.h
    m/tracing/may_forward_message_option.h
    m/tracing/message.h
    m/tracing/message_allocator.h
    m/tracing/message_processor.h
    m/tracing/message_queue.h 
    m/tracing/message_source.h
    m/tracing/monitor_class.h
    m/tracing/monitor_class_var.h 
    m/tracing/multiplexor.h 
    m/tracing/on_message_disposition.h 
    m/tracing/ostream_sink.h
    m/tracing/safe_array_iterator.h 
    m/tracing/sink.h 
    m/tracing/sink_registration.h
    m/tracing/source.h 
    m/tracing/topology_version.h 
    "m/tracing/tracing.h" 
)
