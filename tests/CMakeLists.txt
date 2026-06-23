add_executable(unit_tests test_smoke.cpp)
target_link_libraries(unit_tests PRIVATE studio_core Qt6::Test)
add_test(NAME unit_tests COMMAND unit_tests)
