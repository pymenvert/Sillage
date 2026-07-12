cmake_minimum_required(VERSION 3.24)

project(sillage
    VERSION 0.1.0
    DESCRIPTION "Multi-LiDAR people tracking engine for immersive spaces"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()

add_subdirectory(engine)

if(PROJECT_IS_TOP_LEVEL)
    # Portable layout: everything flat next to the binary, runs from anywhere.
    install(TARGETS sillage-engine RUNTIME DESTINATION .)
    install(DIRECTORY ui/dev/ DESTINATION ui)
    install(DIRECTORY examples DESTINATION .)
    install(DIRECTORY packaging DESTINATION .)
    install(FILES README.md DESTINATION .)
    if(WIN32)
        install(FILES packaging/portable/run-sillage.bat DESTINATION .)
    else()
        install(PROGRAMS packaging/portable/run-sillage.sh DESTINATION .)
    endif()
    install(FILES packaging/portable/LISEZMOI-PORTABLE.txt DESTINATION .)

    set(CPACK_PACKAGE_NAME "sillage")
    set(CPACK_PACKAGE_VENDOR "Sillage")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" SILLAGE_PLATFORM)
    set(CPACK_PACKAGE_FILE_NAME "sillage-${PROJECT_VERSION}-portable-${SILLAGE_PLATFORM}")
    include(CPack)
endif()
