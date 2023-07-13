vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO labapart/gattlib
    REF 7056103e6afdb7722b1dedf3896c8de32880c48a
    SHA512 ca4813fe292314663dd77ee6603d26ec0c10d17b7faef0ea2b3fdeb9a2c141b40b5a9759221748438e99714c822f7444c5fe02f98d130d4bd532b749d2fcc2a9
    HEAD_REF master
)

vcpkg_configure_cmake(
    SOURCE_PATH ${SOURCE_PATH}
    PREFER_NINJA
    OPTIONS -DGATTLIB_SHARED_LIB=OFF -DGATTLIB_BUILD_EXAMPLES=OFF -DGATTLIB_BUILD_DOCS=OFF
)

vcpkg_install_cmake()
vcpkg_copy_pdbs()

# Handle copyright
file(INSTALL ${SOURCE_PATH}/README.md DESTINATION ${CURRENT_PACKAGES_DIR}/share/${PORT} RENAME copyright)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
