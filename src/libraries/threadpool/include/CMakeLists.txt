cmake_minimum_required(VERSION 3.23)

target_sources(m_threadpool PUBLIC FILE_SET HEADERS FILES
    m/threadpool/threadpool.h
    m/threadpool/timer.h
    m/threadpool/types.h
    m/threadpool/work_executor.h
    m/threadpool/work_item_source.h
    m/threadpool/work_item_state.h
    m/threadpool/work_queue.h
    m/threadpool/work_queue_execution_policy.h
)
