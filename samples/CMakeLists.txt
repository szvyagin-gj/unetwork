project(unetwork-samples)

macro(build_sample)
  add_executable(${PROJECT_NAME} ${PROJECT_NAME}.cpp)
  target_link_libraries(${PROJECT_NAME} PRIVATE unetwork-lib)

  set(OUT_DIR ${CMAKE_SOURCE_DIR}/bin/${PROJECT_NAME})
  set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUT_DIR})
  target_compile_definitions(${PROJECT_NAME} PRIVATE "$<$<CONFIG:DEBUG>:DEBUG>")
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${PROJECT_SOURCE_DIR}/config_dev.yaml
    ${OUT_DIR}/config_dev.yaml)

  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${PROJECT_SOURCE_DIR}/dynamic_config_fallback.json
    ${OUT_DIR}/dynamic_config_fallback.json)
endmacro()

add_subdirectory(tcp_echo)
add_subdirectory(http_hello)
