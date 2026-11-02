include_guard(GLOBAL)
if(NOT TARGET ObjectDocument::ObjectDocument)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ObjectDocument")
endif()
