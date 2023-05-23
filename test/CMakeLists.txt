enable_testing()

add_library(catch2 STATIC catch_amalgamated.cpp catch_amalgamated.hpp)

function(shdb_add_test _NAME)
    add_executable(${_NAME} ${_NAME}.cpp)
    target_link_libraries(${_NAME} daw_lib catch2)
    add_test(NAME ${_NAME} COMMAND ${_NAME})
    set_property(TARGET ${_NAME} PROPERTY CXX_STANDARD 20)
endfunction()

shdb_add_test(track_test)