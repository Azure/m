cmake_minimum_required(VERSION 3.23)

find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)

target_sources(m_pil PRIVATE
    openapi_model.cpp
)

# nlohmann_json is exposed through the internal contract headers (body/parameter
# schemas are nlohmann::json), so it must be PUBLIC for in-tree consumers (tests)
# that include those headers. yaml-cpp and the schema validator are
# implementation details of the .cpp and stay PRIVATE.
target_link_libraries(m_pil
    PUBLIC  nlohmann_json::nlohmann_json
    PRIVATE yaml-cpp::yaml-cpp
            nlohmann_json_schema_validator::validator
)
