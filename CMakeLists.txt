cmake_minimum_required(VERSION 3.24)
project(PVA_DualCamCpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# CMake's Ninja/MSVC toolchain file leaves CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES
# empty. cl.exe still finds these directories through the Developer Prompt, but
# VS Code's compile_commands consumer does not inherit that environment. Add
# them explicitly so every translation unit and header gets an IntelliSense
# configuration that can resolve the C++ standard library and Windows SDK.
if(MSVC)
    get_filename_component(PVA_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(PVA_MSVC_ROOT "${PVA_MSVC_BIN_DIR}/../../.." ABSOLUTE)
    set(PVA_MSVC_INCLUDE_DIR "${PVA_MSVC_ROOT}/include")

    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" PVA_WINDOWS_SDK_ROOT)
    string(REGEX REPLACE "[/\\\\]+$" "" PVA_WINDOWS_SDK_VERSION "$ENV{WindowsSDKVersion}")
    set(PVA_WINDOWS_SDK_INCLUDE_ROOT
        "${PVA_WINDOWS_SDK_ROOT}/Include/${PVA_WINDOWS_SDK_VERSION}")

    set(PVA_TOOLCHAIN_INCLUDE_DIRS "${PVA_MSVC_INCLUDE_DIR}")
    foreach(PVA_SDK_COMPONENT ucrt shared um winrt cppwinrt)
        if(EXISTS "${PVA_WINDOWS_SDK_INCLUDE_ROOT}/${PVA_SDK_COMPONENT}")
            list(APPEND PVA_TOOLCHAIN_INCLUDE_DIRS
                "${PVA_WINDOWS_SDK_INCLUDE_ROOT}/${PVA_SDK_COMPONENT}")
        endif()
    endforeach()
    include_directories(BEFORE ${PVA_TOOLCHAIN_INCLUDE_DIRS})
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs)

add_library(open62541 STATIC third_party/open62541/open62541.c)
target_include_directories(open62541 PUBLIC third_party/open62541)
target_compile_definitions(open62541 PRIVATE UA_ENABLE_AMALGAMATION)
if(WIN32)
    target_link_libraries(open62541 PUBLIC ws2_32 iphlpapi)
endif()

set(SAPERA_ROOT "C:/Program Files/Teledyne DALSA/Sapera" CACHE PATH "Teledyne DALSA Sapera SDK root")
set(SAPERA_INCLUDE "${SAPERA_ROOT}/Include")
set(SAPERA_BASIC_INCLUDE "${SAPERA_ROOT}/Classes/Basic")
set(SAPERA_LIBRARY "${SAPERA_ROOT}/Lib/Win64/SapClassBasic.lib")

add_library(pva_measurement
    src/config.cpp
    src/state_store.cpp
    src/measurement_engine.cpp
    src/algorithms/image_utils.cpp
    src/algorithms/neck_detector.cpp
    src/algorithms/reflector_detector.cpp
    src/algorithms/meniscus_detector.cpp
    src/algorithms/endcone_detector.cpp
)
target_include_directories(pva_measurement PUBLIC include)
target_link_libraries(pva_measurement PUBLIC Qt6::Core ${OpenCV_LIBS})
target_compile_definitions(pva_measurement PUBLIC NOMINMAX)

add_executable(pva_dualcam_cpp WIN32
    src/main.cpp
    src/main_window.cpp
    include/main_window.hpp
    src/app_signals.cpp
    src/config_manager.cpp
    include/base_page.hpp
    include/config_manager.hpp
    src/custom_graphics_view.cpp
    src/measurement_worker.cpp
    src/page_home.cpp
    src/page_camera.cpp
    src/camera_manager.cpp
    src/camera_state_store.cpp
    src/dalsa_camera.cpp
    src/opcua_worker.cpp
    src/page_parameters.cpp
    include/app_signals.hpp
    include/custom_graphics_view.hpp
    include/measurement_worker.hpp
    include/page_home.hpp
    include/page_camera.hpp
    include/camera_manager.hpp
    include/camera_state_store.hpp
    include/dalsa_camera.hpp
    include/opcua_worker.hpp
    include/page_parameters.hpp
    ui/main.ui
    ui/PageHome.ui
    ui/PageCamera.ui
    ui/PageParameters.ui
    resources.qrc
)
set_property(TARGET pva_dualcam_cpp PROPERTY AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui")
target_link_libraries(pva_dualcam_cpp PRIVATE pva_measurement open62541 Qt6::Gui Qt6::Widgets)
if(EXISTS "${SAPERA_BASIC_INCLUDE}/SapClassBasic.h" AND EXISTS "${SAPERA_LIBRARY}")
    target_compile_definitions(pva_dualcam_cpp PRIVATE PVA_HAS_SAPERA=1)
    target_include_directories(pva_dualcam_cpp SYSTEM PRIVATE "${SAPERA_INCLUDE}" "${SAPERA_BASIC_INCLUDE}")
    target_link_libraries(pva_dualcam_cpp PRIVATE "${SAPERA_LIBRARY}")
endif()

add_executable(test_cpp
    tools/test.cpp
)

target_link_libraries(test_cpp PRIVATE Qt6::Core)

include(CTest)
if(BUILD_TESTING)
    add_executable(pva_measurement_tests tests/test_measurement.cpp)
    target_link_libraries(pva_measurement_tests PRIVATE pva_measurement)
    add_test(NAME pva_measurement_tests
        COMMAND pva_measurement_tests "${CMAKE_CURRENT_SOURCE_DIR}/cnf.ini")
    if(WIN32)
        set_tests_properties(pva_measurement_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${OpenCV_DIR}/../bin")
    endif()
endif()

install(TARGETS pva_dualcam_cpp RUNTIME DESTINATION .)
