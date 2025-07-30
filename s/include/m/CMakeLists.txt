cmake_minimum_required(VERSION 3.23)

# Produce the final version.h using template version.h.in and substituting variables.
# We don't want to polute our source tree with it, thus putting it in binary tree.
configure_file("version.h.in" "${CMAKE_CURRENT_BINARY_DIR}/version.h" @ONLY)

# utility.h uses tracing which kind of makes it transitively applicable everywhere.
# consider refactoring.
link_libraries(m_tracing)

install(DIRECTORY exception
        DESTINATION ${M_INSTALL_INCLUDEDIR}
)

install(DIRECTORY type_traits
        DESTINATION ${M_INSTALL_INCLUDEDIR}
)

install(DIRECTORY utility
        DESTINATION ${M_INSTALL_INCLUDEDIR}
)

