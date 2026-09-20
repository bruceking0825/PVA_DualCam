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
    # Source files contain Chinese comments and UI strings and are saved as
    # UTF-8.  Older MSVC toolsets otherwise decode them with the active ANSI
    # code page, which can even turn valid code after a comment into syntax
    # errors.
    add_compile_options(/utf-8)

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

# Keep the source compatible with both the existing 64-bit Qt 6 build and the
# locally built 32-bit Qt 5 package.  CMAKE_PREFIX_PATH decides which package
# is selected for a particular build directory.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)
set(PVA_QT_PACKAGE "Qt${QT_VERSION_MAJOR}")
find_package(OpenCV 4 REQUIRED COMPONENTS core imgproc imgcodecs)

set(SAPERA_ROOT "C:/Program Files/Teledyne DALSA/Sapera" CACHE PATH "Teledyne DALSA Sapera SDK root")
set(SAPERA_INCLUDE "${SAPERA_ROOT}/Include")
set(SAPERA_BASIC_INCLUDE "${SAPERA_ROOT}/Classes/Basic")
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(SAPERA_LIBRARY "${SAPERA_ROOT}/Lib/Win32/SapClassBasic.lib")
else()
    set(SAPERA_LIBRARY "${SAPERA_ROOT}/Lib/Win64/SapClassBasic.lib")
endif()

add_library(pva_sherlock_protocol
    src/sherlock_protocol.cpp
)
target_include_directories(pva_sherlock_protocol PUBLIC include)
target_link_libraries(pva_sherlock_protocol PUBLIC ${PVA_QT_PACKAGE}::Core)

add_library(pva_measurement
    src/config.cpp
    src/state_store.cpp
    src/measurement_engine.cpp
    src/algorithms/image_utils.cpp
    src/algorithms/neck_detector.cpp
    src/algorithms/meniscus_detector.cpp
    src/algorithms/endcone_detector.cpp
)
target_include_directories(pva_measurement PUBLIC include)
target_link_libraries(pva_measurement PUBLIC ${PVA_QT_PACKAGE}::Core ${OpenCV_LIBS})
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
    src/sherlock_tcp_server.cpp
    src/page_parameters.cpp
    include/app_signals.hpp
    include/custom_graphics_view.hpp
    include/measurement_worker.hpp
    include/page_home.hpp
    include/page_camera.hpp
    include/camera_manager.hpp
    include/camera_state_store.hpp
    include/dalsa_camera.hpp
    include/sherlock_protocol.hpp
    include/sherlock_tcp_server.hpp
    include/page_parameters.hpp
    ui/main.ui
    ui/PageHome.ui
    ui/PageCamera.ui
    ui/PageParameters.ui
    resources.qrc
)
set_property(TARGET pva_dualcam_cpp PROPERTY AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui")
target_link_libraries(pva_dualcam_cpp PRIVATE pva_measurement pva_sherlock_protocol
    ${PVA_QT_PACKAGE}::Gui ${PVA_QT_PACKAGE}::Widgets ${PVA_QT_PACKAGE}::Network)
if(MSVC)
    # 当前工作站的cvtres.exe处理自动manifest时崩溃；程序不依赖自定义manifest。
    target_link_options(pva_dualcam_cpp PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
endif()
if(EXISTS "${SAPERA_BASIC_INCLUDE}/SapClassBasic.h" AND EXISTS "${SAPERA_LIBRARY}")
    target_compile_definitions(pva_dualcam_cpp PRIVATE PVA_HAS_SAPERA=1)
    target_include_directories(pva_dualcam_cpp SYSTEM PRIVATE "${SAPERA_INCLUDE}" "${SAPERA_BASIC_INCLUDE}")
    target_link_libraries(pva_dualcam_cpp PRIVATE "${SAPERA_LIBRARY}")
endif()

add_executable(test_cpp
    tools/test.cpp
)

target_link_libraries(test_cpp PRIVATE ${PVA_QT_PACKAGE}::Core)
if(MSVC)
    target_link_options(test_cpp PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pva_measurement_tests tests/test_measurement.cpp)
    target_link_libraries(pva_measurement_tests PRIVATE pva_measurement)
    if(MSVC)
        target_link_options(pva_measurement_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME pva_measurement_tests
        COMMAND pva_measurement_tests "${CMAKE_CURRENT_SOURCE_DIR}/cnf.ini")
    add_executable(sherlock_protocol_tests tests/test_sherlock_protocol.cpp)
    target_link_libraries(sherlock_protocol_tests PRIVATE pva_sherlock_protocol)
    if(MSVC)
        # 本机VS资源转换器异常时，纯协议测试不需要嵌入Windows manifest。
        target_link_options(sherlock_protocol_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME sherlock_protocol_tests COMMAND sherlock_protocol_tests)
    if(WIN32)
        set_tests_properties(pva_measurement_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${PVA_QT_PACKAGE}::Core>;PATH=path_list_prepend:${OpenCV_DIR}/../bin")
        set_tests_properties(sherlock_protocol_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${PVA_QT_PACKAGE}::Core>")
    endif()
endif()

install(TARGETS pva_dualcam_cpp RUNTIME DESTINATION .)
