vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO zeux/pugixml
    REF "v${VERSION}"
    SHA512 b8a70f1f230b0902b719346ce0a551eafe534f81262280dceeb92d5ad90ea4e635173e08e225bf66eb5f4724ac4568bd40dc923f184571f02502dac49bc0b7f5
    HEAD_REF master
    PATCHES
        0001-cmakelists-txt-publish-pugixml-cpp.patch
        0002-oacr-warning-pugixml-cpp.patch
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        header-only PUGIXML_HEADER_ONLY
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DPUGIXML_BUILD_TESTS=OFF
        -DPUGIXML_WCHAR_MODE=ON
        ${FEATURE_OPTIONS}
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/${PORT})
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")