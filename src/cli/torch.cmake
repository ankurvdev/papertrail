#use_prebuilt_libtorch(papertrail)
set(CMAKE_CXX_STANDARD 23)
set_target_properties(cli PROPERTIES CXX_STANDARD 23)
target_compile_definitions(cli PRIVATE WEB_USE_STATIC_FILES=\"${CMAKE_CURRENT_SOURCE_DIR}/../web\")