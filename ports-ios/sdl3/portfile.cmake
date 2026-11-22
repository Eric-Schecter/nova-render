vcpkg_download_distfile(archive
    URLS "https://github.com/libsdl-org/SDL/releases/download/release-${VERSION}/SDL3-${VERSION}.tar.gz"
    FILENAME "SDL3-${VERSION}.tar.gz"
    SHA512 6ac2566e58f4db58112dcf328ee829f855f5f3c00329ebb358be706f8958fa980d0e0d7247678b76a8bc6e9137fc4a17424d84105f39de178af3a99680559d63
)
vcpkg_extract_source_archive(source ARCHIVE "${archive}")
vcpkg_cmake_configure(SOURCE_PATH "${source}" OPTIONS
    -DSDL_SHARED=OFF
    -DSDL_STATIC=ON
    -DSDL_TEST_LIBRARY=OFF
    -DSDL_TESTS=OFF
    -DSDL_INSTALL_CMAKEDIR_ROOT=share/sdl3
    -DCMAKE_DISABLE_FIND_PACKAGE_LibUSB=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
vcpkg_fixup_pkgconfig()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${source}/LICENSE.txt")
