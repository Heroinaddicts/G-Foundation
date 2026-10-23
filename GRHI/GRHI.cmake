include_guard(GLOBAL)

# External consumers receive the public API and an existing runtime binary.
# Build GRHI's implementation separately through its own CMakeLists.txt.
if(NOT TARGET GRHI::GRHIApi)
    add_library(GRHIApi INTERFACE)
    add_library(GRHI::GRHIApi ALIAS GRHIApi)
    target_compile_features(GRHIApi INTERFACE cxx_std_20)
    target_include_directories(GRHIApi INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/GRHIApi"
    )
    target_link_libraries(GRHIApi INTERFACE ${CMAKE_DL_LIBS})
endif()

if(NOT TARGET GRHI::GRHI)
    add_library(GRHI::GRHI SHARED IMPORTED GLOBAL)
    get_filename_component(grhi_runtime_directory
        "${CMAKE_CURRENT_LIST_DIR}/../Bin" ABSOLUTE
    )
    if(WIN32)
        set(grhi_runtime_filename "GRHI${CMAKE_SHARED_LIBRARY_SUFFIX}")
        set(grhi_sdl_runtime_filename "SDL3.dll")
    else()
        set(grhi_runtime_filename "${CMAKE_SHARED_LIBRARY_PREFIX}GRHI${CMAKE_SHARED_LIBRARY_SUFFIX}")
        if(APPLE)
            set(grhi_sdl_runtime_filename "libSDL3.0.dylib")
        else()
            set(grhi_sdl_runtime_filename "libSDL3.so.0")
        endif()
    endif()
    set_target_properties(GRHI::GRHI PROPERTIES
        IMPORTED_LOCATION
            "${grhi_runtime_directory}/${grhi_runtime_filename}"
        INTERFACE_LINK_LIBRARIES GRHI::GRHIApi
        GRHI_RUNTIME_DEPENDENCIES "${grhi_runtime_directory}/${grhi_sdl_runtime_filename}"
    )
    if(WIN32)
        set_target_properties(GRHI::GRHI PROPERTIES
            IMPORTED_IMPLIB
                "${grhi_runtime_directory}/${CMAKE_IMPORT_LIBRARY_PREFIX}GRHI${CMAKE_IMPORT_LIBRARY_SUFFIX}"
        )
    endif()
endif()
