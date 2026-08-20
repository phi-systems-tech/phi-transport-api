# jsontext.h is Qt-free, so this target does not link Qt either.
add_executable(transport_jsontext_tests jsontext_tests.cpp)
target_compile_features(transport_jsontext_tests PRIVATE cxx_std_20)
target_include_directories(transport_jsontext_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
add_test(NAME transport_jsontext_tests COMMAND transport_jsontext_tests)

# LogEntry and Error still carry Qt types, so this one does link Qt.
add_executable(transport_types_tests types_tests.cpp)
target_compile_features(transport_types_tests PRIVATE cxx_std_20)
target_include_directories(transport_types_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(transport_types_tests PRIVATE Qt6::Core)
add_test(NAME transport_types_tests COMMAND transport_types_tests)
