cmake_minimum_required(VERSION 3.21)

add_library(LocalDatabaseLMDB STATIC
    mdb.c
    midl.c
    lmdb.h
    midl.h
    LICENSE
    COPYRIGHT
    CHANGES
    intro.doc
    UPSTREAM.txt
)
add_library(LMDB::LMDB ALIAS LocalDatabaseLMDB)

target_compile_features(LocalDatabaseLMDB PRIVATE c_std_99)
target_include_directories(LocalDatabaseLMDB SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_target_properties(LocalDatabaseLMDB PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

find_package(Threads REQUIRED)
target_link_libraries(LocalDatabaseLMDB PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(LocalDatabaseLMDB PRIVATE advapi32)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    mdb.c midl.c lmdb.h midl.h LICENSE COPYRIGHT CHANGES intro.doc UPSTREAM.txt
)
