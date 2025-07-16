cmake_minimum_required(VERSION 3.23)

find_package(Doxygen)

option(BUILD_DOCUMENTATION "Create and install the API documentation (requires Doxygen)" ${DOXYGEN_FOUND})

if(BUILD_DOCUMENTATION)
    if(NOT DOXYGEN_FOUND)
         message(FATAL_ERROR "Doxygen is needed to build the documentation.")
    endif()

    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/docs/html)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/docs/xml)

    set(DOXYGEN_HTML_OUTPUT            ${PROJECT_BINARY_DIR}/docs/html)
    set(DOXYGEN_XML_OUTPUT             ${PROJECT_BINARY_DIR}/docs/xml)
    set(DOXYGEN_JAVADOC_AUTOBRIEF      YES)
    set(DOXYGEN_GENERATE_HTML          YES)
    set(DOXYGEN_GENERATE_XML           YES)
    set(DOXYGEN_HAVE_DOT               YES)
    set(DOXYGEN_USE_MDFILE_AS_MAINPAGE README.md)  # requires a README.md in /myproject/
    set(DOXYGEN_EXCLUDE_PATTERNS       */test/* */vcpkg_installed/* */vcpkg/*)

    # other DOXYGEN_... options

    doxygen_add_docs(docs_target
      ${PROJECT_SOURCE_DIR}
      ALL  # does not seem to work
      COMMENT "Generate documentation"
    )

endif()

