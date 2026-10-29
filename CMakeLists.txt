cmake_minimum_required(VERSION 3.26)

project(AquariusEngine
    VERSION 0.1.0
    LANGUAGES C CXX
)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# =====================================
# Source files
# =====================================

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/cpp_files/*.cpp"
)

file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/header_files/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/header_files/*.hpp"
)


# =====================================
# GLAD
# =====================================

add_library(glad
    external/glad/src/glad.c
)

target_include_directories(glad
    PUBLIC
        external/glad/include
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(IMGUI REQUIRED IMPORTED_TARGET imgui)

add_library(imgui_backends
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_backends
    PUBLIC
        external/imgui
)
target_link_libraries(imgui_backends
    PUBLIC
        PkgConfig::IMGUI
        glfw
)


# =====================================
# Dependencies
# =====================================

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)
find_package(assimp 5 REQUIRED)
find_path(STB_IMAGE_INCLUDE_DIR stb_image.h PATH_SUFFIXES stb REQUIRED)


# =====================================
# Engine executable
# =====================================

add_executable(${PROJECT_NAME}
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

set(AQUARIUS_FONT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/font")
set(AQUARIUS_FONT_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/source/font/AlegreyaSans-Regular.ttf")
set(AQUARIUS_TITLE_FONT_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/source/font/Cinzel-Regular.ttf")
file(MAKE_DIRECTORY "${AQUARIUS_FONT_DIRECTORY}")
configure_file("${AQUARIUS_FONT_FILE}"
               "${AQUARIUS_FONT_DIRECTORY}/AlegreyaSans-Regular.ttf" COPYONLY)
configure_file("${AQUARIUS_TITLE_FONT_FILE}"
               "${AQUARIUS_FONT_DIRECTORY}/Cinzel-Regular.ttf" COPYONLY)

set(AQUARIUS_SPLASH_SOURCE_IMAGE
    "${CMAKE_CURRENT_SOURCE_DIR}/source/graphics/splash_screen/splash.png")
set(AQUARIUS_SPLASH_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/graphics/splash_screen")
set(AQUARIUS_SPLASH_IMAGE
    "${AQUARIUS_SPLASH_DIRECTORY}/splash.png")
file(MAKE_DIRECTORY "${AQUARIUS_SPLASH_DIRECTORY}")
configure_file("${AQUARIUS_SPLASH_SOURCE_IMAGE}" "${AQUARIUS_SPLASH_IMAGE}"
               COPYONLY)

set(AQUARIUS_LIGHT_ICON_SOURCE_DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/source/graphics/light_icons")
set(AQUARIUS_LIGHT_ICON_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/graphics/light_icons")
set(AQUARIUS_LIGHT_ICON_FILES
    directional_light.png
    spot_light.png
    point_light.png
    sky_light.png
    rect_light.png
)
file(MAKE_DIRECTORY "${AQUARIUS_LIGHT_ICON_DIRECTORY}")
foreach(LIGHT_ICON IN LISTS AQUARIUS_LIGHT_ICON_FILES)
    configure_file(
        "${AQUARIUS_LIGHT_ICON_SOURCE_DIRECTORY}/${LIGHT_ICON}"
        "${AQUARIUS_LIGHT_ICON_DIRECTORY}/${LIGHT_ICON}"
        COPYONLY
    )
endforeach()

target_include_directories(${PROJECT_NAME}
    PRIVATE
        source/header_files
        ${STB_IMAGE_INCLUDE_DIR}
)
target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        AQUARIUS_BUNDLED_FONT_PATH=\"${AQUARIUS_FONT_DIRECTORY}/AlegreyaSans-Regular.ttf\"
        AQUARIUS_TITLE_FONT_PATH=\"${AQUARIUS_FONT_DIRECTORY}/Cinzel-Regular.ttf\"
        AQUARIUS_SPLASH_IMAGE_PATH=\"${AQUARIUS_SPLASH_IMAGE}\"
        AQUARIUS_LIGHT_ICON_DIRECTORY=\"${AQUARIUS_LIGHT_ICON_DIRECTORY}\"
)

if(TARGET assimp::assimp)
    set(ASSIMP_TARGET assimp::assimp)
elseif(TARGET assimp)
    set(ASSIMP_TARGET assimp)
else()
    message(FATAL_ERROR "Assimp was found but did not provide a supported CMake target.")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glad
        imgui_backends
        glfw
        ${ASSIMP_TARGET}
        OpenGL::GL
)

if(BUILD_TESTING)
    add_executable(ProjectTests
        tests/project_tests.cpp
        source/cpp_files/Project.cpp
    )
    target_include_directories(ProjectTests
        PRIVATE
            source/header_files
    )
    add_test(NAME ProjectTests COMMAND ProjectTests)

    add_executable(ModelAssetsTests
        tests/model_assets_tests.cpp
        source/cpp_files/EditorScene.cpp
        source/cpp_files/ModelAssets.cpp
        source/cpp_files/SceneRecovery.cpp
    )
    target_include_directories(ModelAssetsTests
        PRIVATE
            source/header_files
    )
    target_compile_definitions(ModelAssetsTests
        PRIVATE
            AQUARIUS_LIGHT_ICON_SOURCE_DIRECTORY=\"${AQUARIUS_LIGHT_ICON_SOURCE_DIRECTORY}\"
    )
    target_link_libraries(ModelAssetsTests
        PRIVATE
            glad
            ${ASSIMP_TARGET}
    )
    add_test(NAME ModelAssetsTests COMMAND ModelAssetsTests)

    add_executable(ModelLoaderTests
        tests/model_loader_tests.cpp
        source/cpp_files/Camera.cpp
        source/cpp_files/EditorScene.cpp
        source/cpp_files/Model.cpp
        source/cpp_files/ModelAssets.cpp
        source/cpp_files/Renderer.cpp
        source/cpp_files/SceneRecovery.cpp
    )
    target_include_directories(ModelLoaderTests
        PRIVATE
            source/header_files
            ${STB_IMAGE_INCLUDE_DIR}
    )
    target_compile_definitions(ModelLoaderTests
        PRIVATE
            AQUARIUS_LIGHT_ICON_DIRECTORY=\"${AQUARIUS_LIGHT_ICON_DIRECTORY}\"
    )
    target_link_libraries(ModelLoaderTests
        PRIVATE
            glad
            glfw
            ${ASSIMP_TARGET}
    )
    add_test(NAME ModelLoaderTests COMMAND ModelLoaderTests)
    set_tests_properties(ModelLoaderTests PROPERTIES SKIP_RETURN_CODE 77)
endif()