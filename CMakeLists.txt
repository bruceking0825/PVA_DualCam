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
    # Ninja 的规则文件使用 UTF-8；无英文语言包时 VSLANG 不生效，必须统一代码页。
    set(PVA_MSVC_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/tools/msvc_utf8.cmd")
    set(CMAKE_C_COMPILER_LAUNCHER "${PVA_MSVC_LAUNCHER}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${PVA_MSVC_LAUNCHER}")
    execute_process(COMMAND "${PVA_MSVC_LAUNCHER}" "${CMAKE_CXX_COMPILER}"
        /nologo /utf-8 /showIncludes /EP "${CMAKE_CURRENT_SOURCE_DIR}/tools/msvc_include_probe.cpp"
        OUTPUT_VARIABLE PVA_INCLUDE_PROBE ERROR_VARIABLE PVA_INCLUDE_PROBE_ERROR ENCODING UTF-8)
    string(REGEX MATCH "(^|\n)([^\r\n]*: +)[A-Za-z]:[/\\\\]" PVA_INCLUDE_MATCH
        "${PVA_INCLUDE_PROBE}\n${PVA_INCLUDE_PROBE_ERROR}")
    if(PVA_INCLUDE_MATCH)
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
        set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_CL_SHOWINCLUDES_PREFIX}")
    else()
        message(FATAL_ERROR "Cannot detect MSVC header dependency prefix; configure from a Developer Prompt")
    endif()
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

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(PVA_SAPERA_LIBRARY_SUBDIR "Win32")
    set(PVA_SAPERA_ROOT_CANDIDATES
        "C:/Program Files (x86)/Teledyne DALSA/Sapera"
        "C:/Program Files/Teledyne DALSA/Sapera")
else()
    set(PVA_SAPERA_LIBRARY_SUBDIR "Win64")
    set(PVA_SAPERA_ROOT_CANDIDATES
        "C:/Program Files/Teledyne DALSA/Sapera"
        "C:/Program Files (x86)/Teledyne DALSA/Sapera")
endif()

# Sapera x86 and x64 installers use different Program Files directories. Keep
# an explicitly configured valid root, but repair an old/invalid cache entry
# automatically after the installed SDK architecture changes.
set(SAPERA_ROOT "" CACHE PATH "Teledyne DALSA Sapera SDK root")
set(PVA_SAPERA_HEADER "${SAPERA_ROOT}/Classes/Basic/SapClassBasic.h")
set(PVA_SAPERA_LIBRARY "${SAPERA_ROOT}/Lib/${PVA_SAPERA_LIBRARY_SUBDIR}/SapClassBasic.lib")
if(NOT EXISTS "${PVA_SAPERA_HEADER}" OR NOT EXISTS "${PVA_SAPERA_LIBRARY}")
    foreach(PVA_SAPERA_CANDIDATE IN LISTS PVA_SAPERA_ROOT_CANDIDATES)
        if(EXISTS "${PVA_SAPERA_CANDIDATE}/Classes/Basic/SapClassBasic.h" AND
           EXISTS "${PVA_SAPERA_CANDIDATE}/Lib/${PVA_SAPERA_LIBRARY_SUBDIR}/SapClassBasic.lib")
            set(SAPERA_ROOT "${PVA_SAPERA_CANDIDATE}" CACHE PATH
                "Teledyne DALSA Sapera SDK root" FORCE)
            break()
        endif()
    endforeach()
endif()
set(SAPERA_INCLUDE "${SAPERA_ROOT}/Include")
set(SAPERA_BASIC_INCLUDE "${SAPERA_ROOT}/Classes/Basic")
set(SAPERA_LIBRARY "${SAPERA_ROOT}/Lib/${PVA_SAPERA_LIBRARY_SUBDIR}/SapClassBasic.lib")

# The SDK may install both Win32 and Win64 import libraries even when only one
# runtime architecture is present. Linking that import library produces an EXE
# which waits at the Windows loader error dialog before main() is reached. Check
# the architecture-specific runtime as well as the headers and import library.
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        file(GLOB PVA_SAPERA_RUNTIME_DLLS
            "$ENV{WINDIR}/SysWOW64/SapClassBasic*.dll"
            "${SAPERA_ROOT}/Bin/SapClassBasic*.dll")
    else()
        file(GLOB PVA_SAPERA_RUNTIME_DLLS
            "$ENV{WINDIR}/System32/SapClassBasic*.dll"
            "${SAPERA_ROOT}/Bin64/SapClassBasic*.dll")
    endif()
endif()

add_library(pva_sherlock_protocol
    src/sherlock_protocol.cpp
    src/measurement_payload.cpp
)
target_include_directories(pva_sherlock_protocol PUBLIC include)
target_link_libraries(pva_sherlock_protocol PUBLIC ${PVA_QT_PACKAGE}::Core ${OpenCV_LIBS})

add_library(pva_measurement
    src/config.cpp
    src/daily_log.cpp

    src/state_store.cpp
    src/measurement_engine.cpp
    src/measurement_geometry.cpp
    src/algorithms/image_utils.cpp
    src/algorithms/neck_detector.cpp
    src/algorithms/meniscus_detector.cpp
)
target_include_directories(pva_measurement PUBLIC include)
target_link_libraries(pva_measurement PUBLIC ${PVA_QT_PACKAGE}::Core ${OpenCV_LIBS})
target_compile_definitions(pva_measurement PUBLIC NOMINMAX)


# 运行服务不依赖 Widgets，可在 QCoreApplication 下进行完整业务回归。
add_library(pva_runtime
    src/runtime_controller.cpp
    include/runtime_controller.hpp
    src/plc_session.cpp
    include/plc_session.hpp
    src/offline_image_source.cpp
    src/image_storage.cpp
    include/image_storage.hpp
    src/measurement_worker.cpp
    include/measurement_worker.hpp
    src/sherlock_tcp_server.cpp
    include/sherlock_tcp_server.hpp
    src/plc_runtime.cpp
)
target_include_directories(pva_runtime PUBLIC include)
target_link_libraries(pva_runtime PUBLIC pva_measurement pva_sherlock_protocol ${PVA_QT_PACKAGE}::Network)

add_library(pva_camera
    src/camera_service.cpp
    include/camera_service.hpp
    src/camera_manager.cpp
    include/camera_manager.hpp
    src/camera_state_store.cpp
    include/camera_state_store.hpp
    src/dalsa_camera.cpp
    include/dalsa_camera.hpp
)
target_include_directories(pva_camera PUBLIC include)
target_link_libraries(pva_camera PUBLIC pva_measurement ${PVA_QT_PACKAGE}::Core)

add_executable(pva_dualcam_cpp WIN32
    src/main.cpp
    src/main_window.cpp
    include/main_window.hpp
    src/app_signals.cpp
    src/config_manager.cpp
    include/base_page.hpp
    include/config_manager.hpp
    src/custom_graphics_view.cpp

    src/page_home.cpp
    src/page_camera.cpp

    src/app_services.cpp
    include/app_services.hpp

    src/page_parameters.cpp
    include/app_signals.hpp
    include/custom_graphics_view.hpp

    include/page_home.hpp
    include/daily_log.hpp
    include/plc_runtime.hpp
    include/page_camera.hpp

    include/sherlock_protocol.hpp

    include/page_parameters.hpp
    ui/main.ui
    ui/PageHome.ui
    ui/PageCamera.ui
    ui/PageParameters.ui
    resources.qrc
)
set_property(TARGET pva_dualcam_cpp PROPERTY AUTOUIC_SEARCH_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui")
target_link_libraries(pva_dualcam_cpp PRIVATE pva_runtime pva_camera
    ${PVA_QT_PACKAGE}::Gui ${PVA_QT_PACKAGE}::Widgets ${PVA_QT_PACKAGE}::Network)
if(MSVC)
    # 当前工作站的cvtres.exe处理自动manifest时崩溃；程序不依赖自定义manifest。
    target_link_options(pva_dualcam_cpp PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
endif()
if(EXISTS "${SAPERA_BASIC_INCLUDE}/SapClassBasic.h" AND
   EXISTS "${SAPERA_LIBRARY}" AND
   (NOT WIN32 OR PVA_SAPERA_RUNTIME_DLLS))
    message(STATUS "Sapera SDK enabled: ${SAPERA_ROOT} (${PVA_SAPERA_LIBRARY_SUBDIR})")
    target_compile_definitions(pva_camera PRIVATE PVA_HAS_SAPERA=1)
    target_include_directories(pva_camera SYSTEM PRIVATE "${SAPERA_INCLUDE}" "${SAPERA_BASIC_INCLUDE}")
    target_link_libraries(pva_camera PRIVATE "${SAPERA_LIBRARY}")
else()
    message(WARNING
        "Sapera SDK/runtime not found for ${PVA_SAPERA_LIBRARY_SUBDIR}; camera support is disabled")
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
    add_executable(acquisition_switch_tests tests/test_acquisition_switch.cpp)
    target_link_libraries(acquisition_switch_tests PRIVATE pva_runtime pva_camera)
    if(MSVC)
        target_link_options(acquisition_switch_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME acquisition_switch_tests COMMAND acquisition_switch_tests)
    set_tests_properties(acquisition_switch_tests PROPERTIES TIMEOUT 40)
    add_executable(auto_exposure_tests tests/test_auto_exposure.cpp)
    target_link_libraries(auto_exposure_tests PRIVATE pva_runtime pva_camera)
    if(MSVC)
        target_link_options(auto_exposure_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME auto_exposure_tests COMMAND auto_exposure_tests)
    set_tests_properties(auto_exposure_tests PROPERTIES TIMEOUT 30)
    add_executable(state_save_tests tests/test_state_save.cpp)
    target_link_libraries(state_save_tests PRIVATE pva_runtime)
    if(MSVC)
        target_link_options(state_save_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME state_save_tests COMMAND state_save_tests)
    set_tests_properties(state_save_tests PROPERTIES TIMEOUT 30)
    add_executable(service_lifecycle_tests
        tests/test_services.cpp src/app_services.cpp src/config_manager.cpp src/app_signals.cpp
        include/app_services.hpp include/config_manager.hpp include/app_signals.hpp)
    target_link_libraries(service_lifecycle_tests PRIVATE pva_runtime pva_camera)
    if(MSVC)
        target_link_options(service_lifecycle_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME service_lifecycle_tests COMMAND service_lifecycle_tests)
    add_executable(runtime_tests tests/test_runtime.cpp)
    target_link_libraries(runtime_tests PRIVATE pva_runtime pva_camera)
    if(MSVC)
        target_link_options(runtime_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME runtime_tests COMMAND runtime_tests "${CMAKE_CURRENT_SOURCE_DIR}/cnf.ini")
    add_executable(connection_health_tests tests/test_connection_health.cpp)
    target_link_libraries(connection_health_tests PRIVATE pva_runtime pva_camera)
    if(MSVC)
        target_link_options(connection_health_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME connection_health_tests COMMAND connection_health_tests)
    add_executable(pva_measurement_tests tests/test_measurement.cpp)
    target_link_libraries(pva_measurement_tests PRIVATE pva_runtime)
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
    set(PVA_HOME_TEST_SOURCES
        src/app_signals.cpp
        src/config_manager.cpp
        src/custom_graphics_view.cpp

        src/page_home.cpp

        include/base_page.hpp
        include/app_signals.hpp
        include/config_manager.hpp
        include/custom_graphics_view.hpp

        include/page_home.hpp

        ui/PageHome.ui
        resources.qrc
    )
    add_executable(facette_page_tests tests/test_facette_page.cpp ${PVA_HOME_TEST_SOURCES})
    add_executable(runtime_ui_benchmark tests/runtime_ui_benchmark.cpp ${PVA_HOME_TEST_SOURCES})
    set_property(TARGET runtime_ui_benchmark PROPERTY AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(runtime_ui_benchmark PRIVATE pva_runtime ${PVA_QT_PACKAGE}::Widgets)
    if(WIN32)
        target_link_libraries(runtime_ui_benchmark PRIVATE Psapi)
    endif()
    if(MSVC)
        target_link_options(runtime_ui_benchmark PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    set_property(TARGET facette_page_tests PROPERTY AUTOUIC_SEARCH_PATHS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(facette_page_tests PRIVATE pva_runtime
        ${PVA_QT_PACKAGE}::Gui ${PVA_QT_PACKAGE}::Widgets ${PVA_QT_PACKAGE}::Network)
    if(MSVC)
        target_link_options(facette_page_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME facette_page_tests
        COMMAND facette_page_tests "${CMAKE_CURRENT_SOURCE_DIR}/cnf.ini")
    add_executable(parameters_page_tests tests/test_parameters_page.cpp
        src/page_parameters.cpp src/config_manager.cpp src/app_signals.cpp
        include/page_parameters.hpp include/base_page.hpp include/config_manager.hpp include/app_signals.hpp
        ui/PageParameters.ui resources.qrc)
    set_property(TARGET parameters_page_tests PROPERTY AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")
    target_link_libraries(parameters_page_tests PRIVATE pva_measurement ${PVA_QT_PACKAGE}::Widgets)
    if(MSVC)
        target_link_options(parameters_page_tests PRIVATE /MANIFEST:NO /INCREMENTAL:NO)
    endif()
    add_test(NAME parameters_page_tests COMMAND parameters_page_tests)
    set_tests_properties(parameters_page_tests PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(WIN32)
        set_tests_properties(pva_measurement_tests runtime_tests service_lifecycle_tests auto_exposure_tests state_save_tests acquisition_switch_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${PVA_QT_PACKAGE}::Core>;PATH=path_list_prepend:${OpenCV_DIR}/../bin")
        set_tests_properties(sherlock_protocol_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${PVA_QT_PACKAGE}::Core>")
        set_tests_properties(facette_page_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${PVA_QT_PACKAGE}::Core>;PATH=path_list_prepend:${OpenCV_DIR}/../bin;QT_QPA_PLATFORM=set:offscreen")
    endif()
    set_tests_properties(service_lifecycle_tests runtime_tests pva_measurement_tests sherlock_protocol_tests facette_page_tests
        PROPERTIES TIMEOUT 30)
endif()

install(TARGETS pva_dualcam_cpp RUNTIME DESTINATION .)
