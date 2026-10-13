cmake_minimum_required(VERSION 3.23)
project(TPFanCtrlModern LANGUAGES CXX)

add_executable(TPFanCtrlModern WIN32 main.cpp)
target_compile_features(TPFanCtrlModern PRIVATE cxx_std_17)
target_compile_definitions(TPFanCtrlModern PRIVATE
  UNICODE _UNICODE)
target_link_libraries(TPFanCtrlModern PRIVATE dwmapi gdi32 user32 advapi32)

if(MSVC)
  target_compile_options(TPFanCtrlModern PRIVATE /W4 /EHsc /utf-8)
endif()
