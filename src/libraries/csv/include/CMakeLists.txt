cmake_minimum_required(VERSION 3.23)

target_sources(m_csv PUBLIC FILE_SET HEADERS FILES 
    m/csv/breaker.h 
    m/csv/field_quoter.h 
    m/csv/writer.h 
)
