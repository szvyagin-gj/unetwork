project(unetwork-lib)

file(GLOB_RECURSE SOURCE_FILES *.hpp *.cpp)

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC userver-core)
target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

