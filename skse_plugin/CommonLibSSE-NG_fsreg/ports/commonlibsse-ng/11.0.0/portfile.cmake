vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibVR
    REF 94faaed0c60eddd8347767f2d4d29a97c93bde8c
    SHA512 d1753f0744608b9ecc60dc57b9d49fa6d846a45f208550fa734cb42e53dba79ee84147658c77bc4ad56fd7457a8d63b54b53a2c28e47f72572d71021abf9791b
)

# Build the pinned source, never a sibling or downloaded prebuilt library. SH2 supplies
# strict call-site guards; CommonLib's optional log-and-continue decoder is not a guard.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DENABLE_SKYRIM_SE=ON
        -DENABLE_SKYRIM_AE=ON
        -DENABLE_SKYRIM_VR=OFF
        -DSKSE_SUPPORT_XBYAK=ON
        -DSKSE_SUPPORT_PATCH_SAFETY=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)
vcpkg_copy_pdbs()
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING.txt")
