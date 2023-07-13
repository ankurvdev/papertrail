function(link_to_target targetName)
    vcpkg_download(cxxopts)
    find_package(cxxopts CONFIG REQUIRED)
    target_link_libraries(${targetName} PRIVATE cxxopts::cxxopts)
endfunction()