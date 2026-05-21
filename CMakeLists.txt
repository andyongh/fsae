    cmake_minimum_required(VERSION 3.5)
    project(fsae VERSION 0.1.0)

    include(CTest)
    enable_testing()

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # Get macOS major.minor version
        execute_process(
            COMMAND sh -c "sw_vers -productVersion | cut -d. -f1-2"
            OUTPUT_VARIABLE MACOSX_TARGET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        # Set the deployment target for CMake and child processes
        set(ENV{MACOSX_DEPLOYMENT_TARGET} "${MACOSX_TARGET}")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${MACOSX_TARGET}")

        # Force load jemalloc on macOS so its zone allocator constructor is not optimized out
        set(JEMALLOC_LINK "-Wl,-force_load,${CMAKE_CURRENT_SOURCE_DIR}/deps/jemalloc/lib/libjemalloc.a" "-lc++")
    else()
        # Force load jemalloc on Linux so its weak symbols override glibc malloc
        set(JEMALLOC_LINK "-Wl,--whole-archive" "${CMAKE_CURRENT_SOURCE_DIR}/deps/jemalloc/lib/libjemalloc.a" "-Wl,--no-whole-archive" "-lstdc++")
    endif()
    string(REPLACE ";" " " JEMALLOC_LINK_STR "${JEMALLOC_LINK}")
    # Append to global executable and shared library linker flags
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${JEMALLOC_LINK_STR}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${JEMALLOC_LINK_STR}")

    # -- jemalloc submodule: auto-init if missing ---------------------------------
    find_package(Git REQUIRED)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/jemalloc/autogen.sh")
        message(STATUS "Submodule jemalloc not found. Initializing...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMOD_RESULT
        )
        if(NOT GIT_SUBMOD_RESULT EQUAL 0)
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}")
        endif()
    endif()

    # -- Build jemalloc as an ExternalProject ------------------------------------
    include(ExternalProject)
    set(JEMALLOC_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/deps/jemalloc)
    # set(JEMALLOC_STATIC_LIB ${JEMALLOC_PREFIX}/lib/libjemalloc.a)
    set(JEMALLOC_STATIC_LIB ${JEMALLOC_PREFIX}/lib/libjemalloc_pic.a)

    ExternalProject_Add(jemalloc_project
        SOURCE_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/deps/jemalloc
        BUILD_IN_SOURCE 1
        CONFIGURE_COMMAND
            sh autogen.sh
            --with-jemalloc-prefix=je_
            --prefix=${JEMALLOC_PREFIX}
            --disable-shared
            --with-pic
        BUILD_COMMAND   make -j4
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${JEMALLOC_STATIC_LIB}
    )

    # Pre-create the include dir so CMake doesn't fail validation at configure time
    # (the real files are produced during the jemalloc build step)
    file(MAKE_DIRECTORY ${JEMALLOC_PREFIX}/include)

    # Imported target so we can use target_link_libraries()
    add_library(jemalloc_imported STATIC IMPORTED GLOBAL)
    set_target_properties(jemalloc_imported PROPERTIES
        IMPORTED_LOCATION             ${JEMALLOC_STATIC_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${JEMALLOC_PREFIX}/include
    )
    add_dependencies(jemalloc_imported jemalloc_project)

    # -- Build the fsae event-loop library ----------------------------------------
    add_library(fsae STATIC ae.c)
    target_include_directories(fsae PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(fsae PRIVATE USE_JEMALLOC)
    target_link_libraries(fsae PRIVATE jemalloc_imported)
    add_dependencies(fsae jemalloc_project)

    # -- Build the example executable ---------------------------------------------
    add_executable(fsae_example examples/main.c)
    if(APPLE)
        target_link_options(fsae_example PRIVATE "-Wl,-force_flat_namespace")
    endif()
    target_link_libraries(fsae_example fsae)

    set(CPACK_PROJECT_NAME ${PROJECT_NAME})
    set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
    include(CPack)
