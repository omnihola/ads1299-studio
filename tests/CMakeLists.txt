add_executable(unit_tests test_smoke.cpp)
set_target_properties(unit_tests PROPERTIES AUTOMOC ON)
target_link_libraries(unit_tests PRIVATE studio_core Qt6::Test)
add_test(NAME unit_tests COMMAND unit_tests)

add_executable(test_eegframe test_eegframe.cpp)
set_target_properties(test_eegframe PROPERTIES AUTOMOC ON)
target_link_libraries(test_eegframe PRIVATE studio_core Qt6::Test)
add_test(NAME test_eegframe COMMAND test_eegframe)
