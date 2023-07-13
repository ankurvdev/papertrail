function(link_to_target targetName)
    if (NOT TARGET usb-1.0)
        vcpkg_download(libusb)
        add_library(usb-1.0 INTERFACE)
        find_package(libusb CONFIG REQUIRED)
        target_link_libraries(usb-1.0 INTERFACE ${LIBUSB_LIBRARIES})
        target_include_directories(usb-1.0 INTERFACE "${LIBUSB_INCLUDE_DIR}")
    endif()
    target_link_libraries(${targetName} PRIVATE usb-1.0)
endfunction()
