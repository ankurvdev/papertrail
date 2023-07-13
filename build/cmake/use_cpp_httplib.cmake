function(link_to_target targetName)
    vcpkg_download(cpp-httplib)
    vcpkg_download(openssl)

    find_path(CPP_HTTPLIB_INCLUDE_DIRS "httplib.h")
    target_include_directories(${targetName} PRIVATE ${CPP_HTTPLIB_INCLUDE_DIRS})

    find_package(OpenSSL REQUIRED)
    target_link_libraries(${targetName} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endfunction()
