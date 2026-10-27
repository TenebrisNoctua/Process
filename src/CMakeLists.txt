luwow_add_library(Luwow.ProcessLibrary
    NAME process
    MODES SERIAL
    SOURCES
        Library.h
        Library.cpp
        Spawn.h
        Spawn.cpp
)
target_include_directories(Luwow.ProcessLibrary PRIVATE ${PROCESS_ROOT}/src)
if(WIN32)
    target_compile_definitions(Luwow.ProcessLibrary PRIVATE NOMINMAX)
    target_link_libraries(Luwow.ProcessLibrary PRIVATE shell32 user32)
endif()
