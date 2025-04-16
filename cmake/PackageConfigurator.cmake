# Create package-config files :
#  - <projectname>_config_version.cmake
#  - <projectname>_config.cmake
# They are installed in lib/cmake/<projectname>.
#
# Required variables :
#  - VERSION
#  - PROJECT_NAME
#

# Include needed for 'write_basic_package_version_file'
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}_config_version.cmake"
  VERSION ${VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_file(cmake/${PROJECT_NAME}_config.cmake
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}_config.cmake"
  COPYONLY
)

# Destination
set(config_install_dir lib/cmake/${PROJECT_NAME})

# Config installation
#   * <prefix>/lib/cmake/<project>/<project>_targets.cmake
install(
  EXPORT ${PROJECT_NAME}_targets
  DESTINATION ${config_install_dir}
)

# Config installation
#   * <prefix>/lib/cmake/<project>/<project>_config.cmake
#   * <prefix>/lib/cmake/<project>/<project>_config_version.cmake
install(
  FILES
    cmake/${PROJECT_NAME}_config.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/${PROJECT_NAME}_config_version.cmake"
  DESTINATION ${config_install_dir}
  COMPONENT devel
)