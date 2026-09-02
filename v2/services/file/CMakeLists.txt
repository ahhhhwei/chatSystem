add_library(file_store
    src/file_store.cc
)

target_include_directories(file_store
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(file_store PUBLIC cxx_std_17)

add_executable(file_demo
    app/main.cc
)

target_link_libraries(file_demo PRIVATE file_store)
