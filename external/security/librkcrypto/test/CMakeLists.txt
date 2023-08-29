file(GLOB TEST_SOURCES "./*.c")
file(GLOB C_MODE_SOURCES "./c_mode/*.c")
include_directories(include)
include_directories(include/c_mode)
add_executable(librkcrypto_test ${TEST_SOURCES} ${C_MODE_SOURCES})

target_link_libraries(librkcrypto_test rkcrypto)
