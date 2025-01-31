cmake_minimum_required(VERSION 3.12) # Increased minimum version to support CMP0079 and modern features

# Enable policies for modern CMake behavior
if (POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW) # Allows linking libraries from other directories
endif()

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("YupEngineRHI")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set(DONUT_WITH_STATIC_SHADERS ON)
set(DONUT_SHADERS_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Shaders")

# Find source files
file(GLOB YupEngineRHI_src
    src/*.cpp
    src/*.h
)

# Set the FFmpeg root directory
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/Libraries/ffmpeg")

# Include FFmpeg headers
include_directories("${FFMPEG_ROOT}/include")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(YUP_ENGINE_BUILD_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(YUP_ENGINE_BUILD_RELEASE)
endif()

if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /DYUP_ENGINE_BUILD_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DYUP_ENGINE_BUILD_RELEASE")
endif()

# Add source files to the main executable
# Full library paths
if(MSVC)
    add_compile_definitions(YUP_ENGINE_PLATFORM_WINDOWS)
    add_executable(YupEngineRHI WIN32 ${YupEngineRHI_src} "src/VideoRenderer.h" "src/VideoRenderer.cpp" "src/FullScreenYUV.cpp" "src/FullScreenYUV.h")
    set(FMOD_ROOT "${CMAKE_SOURCE_DIR}/Libraries/fmod/win")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
       set(FMOD_LIBRARIES
        "${FMOD_ROOT}/core/lib/x64/fmodL.lib")
    else ()
        set(FMOD_LIBRARIES
        "${FMOD_ROOT}/core/lib/x64/fmod.lib")
    endif ()
    set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib/win")
    # Add required Windows libraries
    set(SYSTEM_LIBRARIES
        bcrypt    # For cryptographic functions
        ws2_32    # For socket functions
        advapi32  # For additional Windows API functions
    )
else()
    add_compile_definitions(YUP_ENGINE_PLATFORM_LINUX)
    add_executable(YupEngineRHI ${YupEngineRHI_src})
    set(FMOD_ROOT "${CMAKE_SOURCE_DIR}/Libraries/fmod/linux")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
       set(FMOD_LIBRARIES
        "${FMOD_ROOT}/core/lib/x64/fmodL.so")
    else ()
        set(FMOD_LIBRARIES
        "${FMOD_ROOT}/core/lib/x64/fmod.so")
    endif ()
    set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib/linux")
    set(SYSTEM_LIBRARIES
        pthread    # For threading support
        dl         # For dynamic loading
        m          # Math library
    )
endif()

# Add Donut library
add_subdirectory(Libraries/donut)

# Include shader compilation
include(Libraries/donut/compileshaders.cmake)

# Compile shaders
file(GLOB shaders "${CMAKE_CURRENT_SOURCE_DIR}/shaders_sources/*.hlsl")
donut_compile_shaders_all_platforms(
    TARGET YupEngineRHI_shaders
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders_sources/shaders.cfg
    FOLDER ${CMAKE_SOURCE_DIR}
    OUTPUT_BASE ${DONUT_SHADERS_OUTPUT_DIR}
)

# Include directories
set(FMOD_INCLUDE_DIRS
    "${FMOD_ROOT}/core/inc"
)

# Library paths
set(FMOD_LIBRARY_PATHS
    "${FMOD_ROOT}/core/lib/x64"
)

# Add include directories
include_directories(${FMOD_INCLUDE_DIRS})

# Link FMOD libraries
add_library(FMOD_LIBS INTERFACE)
target_include_directories(FMOD_LIBS INTERFACE ${FMOD_INCLUDE_DIRS})
target_link_directories(FMOD_LIBS INTERFACE ${FMOD_LIBRARY_PATHS})
target_link_libraries(FMOD_LIBS INTERFACE ${FMOD_LIBRARIES})

file(GLOB FFMPEG_LIBRARIES "${FFMPEG_LIB_DIR}/lib*.a")


# Link against Donut libraries
target_link_libraries(YupEngineRHI donut_app donut_engine donut_render ${FMOD_LIBRARIES} ${SYSTEM_LIBRARIES} ${FFMPEG_LIBRARIES})

# Add dependencies
add_dependencies(YupEngineRHI YupEngineRHI_shaders)

# Set target properties
set_target_properties(YupEngineRHI PROPERTIES FOLDER "Applications")

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
    set_target_properties(YupEngineRHI PROPERTIES VS_USER_PROPS "${CMAKE_SOURCE_DIR}/build.props")
endif()

# Paths for assets and executables
set(ASSETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Assets")
set(ASSETS_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Assets")
set(ZIP_FILE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/Assets.zip")
set(HASH_FILE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets.hash")

# Determine platform-specific executables
if(WIN32)
    set(ZIP_EXECUTABLE "${CMAKE_SOURCE_DIR}/utilities/7za.exe")
    set(HASHER_EXECUTABLE "${CMAKE_SOURCE_DIR}/utilities/YupHasher.exe")
else()
    set(ZIP_EXECUTABLE "${CMAKE_SOURCE_DIR}/utilities/7zzs")
    set(HASHER_EXECUTABLE "${CMAKE_SOURCE_DIR}/utilities/YupHasher")
endif()

# Step 1: Generate hash of the Assets directory
add_custom_command(
    TARGET YupEngineRHI POST_BUILD
    COMMAND "${HASHER_EXECUTABLE}" "${ASSETS_SOURCE_DIR}" > "${HASH_FILE}"
)

# Step 2: Check if copying or compressing is needed
add_custom_command(
    TARGET YupEngineRHI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E compare_files "${HASH_FILE}" "${HASH_FILE}.old" || (
        # Copy Assets directory only if needed
        ${CMAKE_COMMAND} -E remove_directory "${ASSETS_OUTPUT_DIR}" &&
        ${CMAKE_COMMAND} -E copy_directory "${ASSETS_SOURCE_DIR}" "${ASSETS_OUTPUT_DIR}" &&
        echo "Assets directory copied."

        # Compress Assets directory and clean up
        && "${ZIP_EXECUTABLE}" a -mx=9 -mmt=16 "${ZIP_FILE}" "${ASSETS_OUTPUT_DIR}/*" && # remember to set to 9 for release build
        ${CMAKE_COMMAND} -E remove_directory "${ASSETS_OUTPUT_DIR}" &&
        echo "Assets directory compressed and removed."

        # Save the new hash
        && ${CMAKE_COMMAND} -E copy "${HASH_FILE}" "${HASH_FILE}.old"
    )
    COMMENT "Checking if Assets directory copy and compression are needed"
)

# Copy dynamically linked libraries:
# Copy DLLs or SOs to the output directory (POST_BUILD)
if(MSVC)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET YupEngineRHI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${FMOD_ROOT}/core/lib/x64/fmodL.dll" $<TARGET_FILE_DIR:YupEngineRHI>
        )
    else()
        add_custom_command(TARGET YupEngineRHI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${FMOD_ROOT}/core/lib/x64/fmod.dll" $<TARGET_FILE_DIR:YupEngineRHI>
        )
    endif()
else()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET YupEngineRHI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${FMOD_ROOT}/core/lib/x64/libfmodL.so" $<TARGET_FILE_DIR:YupEngineRHI>
        )
    else ()
        add_custom_command(TARGET YupEngineRHI POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${FMOD_ROOT}/core/lib/x64/libfmod.so" $<TARGET_FILE_DIR:YupEngineRHI>
        )
    endif ()
endif()