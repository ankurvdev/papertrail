function(link_to_target targetName)
    if (NOT TARGET rtlsdr)
        vcpkg_download(rtlsdr)
        add_library(rtlsdr INTERFACE)

        find_vcpkg_library(librtlsdr rtlsdr)
        find_path(hdr rtl-sdr.h REQUIRED)
        target_link_libraries(rtlsdr INTERFACE ${librtlsdr})
        target_include_directories(rtlsdr INTERFACE "${hdr}")
        target_compile_definitions(rtlsdr INTERFACE rtlsdr_STATIC=1)

        find_package(libusb CONFIG REQUIRED)
        add_library(libusb INTERFACE)
        target_include_directories(libusb INTERFACE ${LIBUSB_INCLUDE_DIRS})
        target_link_libraries(libusb INTERFACE ${LIBUSB_LIBRARIES})
        if (UNIX AND NOT ANDROID)
            target_link_libraries(libusb INTERFACE udev)
        endif()
        target_link_libraries(rtlsdr INTERFACE libusb)
        add_library(rtlsdr::rtlsdr ALIAS rtlsdr)
    endif()
    target_link_libraries(${targetName} PRIVATE rtlsdr libusb)
endfunction()