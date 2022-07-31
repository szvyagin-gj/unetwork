project(unetwork-lib)

file(GLOB SOURCE_FILES *.hpp *.cpp)
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PUBLIC userver-core)
