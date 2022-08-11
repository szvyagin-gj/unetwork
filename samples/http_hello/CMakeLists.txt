project(http_hello)

build_sample()
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${PROJECT_SOURCE_DIR}/server.crt
    ${OUT_DIR}/server.crt)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${PROJECT_SOURCE_DIR}/server.key
    ${OUT_DIR}/server.key)
