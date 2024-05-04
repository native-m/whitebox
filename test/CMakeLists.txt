add_library(catch catch_amalgamated.cpp catch_amalgamated.hpp)

macro(wb_add_test _NAME _SOURCE)
    add_executable(${_NAME} ${_SOURCE})
    target_link_libraries(${_NAME} catch whitebox-lib)
    add_test(NAME test_${_NAME} COMMAND ${_NAME})
endmacro(wb_add_test)

wb_add_test(test_audio_buffer test_audio_buffer.cpp)
wb_add_test(test_pulseaudio_devicelist test_pulseaudio_devicelist.cpp)
# wb_add_test(<test name> <source file>)
