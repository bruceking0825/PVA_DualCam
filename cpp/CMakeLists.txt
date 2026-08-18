cmake_minimum_required(VERSION 3.24)
project(PVA_DualCamCpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

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
    include/pva/main_window.hpp
    src/app_signals.cpp
    src/config_manager.cpp
    include/pva/base_page.hpp
    include/pva/config_manager.hpp
    src/custom_graphics_view.cpp
    src/measurement_worker.cpp
    src/page_home.cpp
    src/page_camera.cpp
    src/camera_manager.cpp
    src/camera_state_store.cpp
    src/dalsa_camera.cpp
    src/opcua_worker.cpp
    src/page_parameters.cpp
    include/pva/app_signals.hpp
    include/pva/custom_graphics_view.hpp
    include/pva/measurement_worker.hpp
    include/pva/page_home.hpp
    include/pva/page_camera.hpp
    include/pva/camera_manager.hpp
    include/pva/camera_state_store.hpp
    include/pva/dalsa_camera.hpp
    include/pva/opcua_worker.hpp
    include/pva/page_parameters.hpp
    ../src/modules/main.ui
    ../src/modules/PageHome.ui
    ../src/modules/PageCamera.ui
    ../src/modules/PageParameters.ui
    ../src/resources.qrc
)
set_property(TARGET pva_dualcam_cpp PROPERTY AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/modules")
target_include_directories(pva_dualcam_cpp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(pva_dualcam_cpp PRIVATE pva_measurement open62541 Qt6::Gui Qt6::Widgets)
if(EXISTS "${SAPERA_BASIC_INCLUDE}/SapClassBasic.h" AND EXISTS "${SAPERA_LIBRARY}")
    target_compile_definitions(pva_dualcam_cpp PRIVATE PVA_HAS_SAPERA=1)
    target_include_directories(pva_dualcam_cpp SYSTEM PRIVATE "${SAPERA_INCLUDE}" "${SAPERA_BASIC_INCLUDE}")
    target_link_libraries(pva_dualcam_cpp PRIVATE "${SAPERA_LIBRARY}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(pva_measurement_tests tests/test_measurement.cpp)
    target_link_libraries(pva_measurement_tests PRIVATE pva_measurement)
    add_test(NAME pva_measurement_tests
        COMMAND pva_measurement_tests "${CMAKE_CURRENT_SOURCE_DIR}/../src/cnf.ini")
    if(WIN32)
        set_tests_properties(pva_measurement_tests PROPERTIES
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:${OpenCV_DIR}/../bin")
    endif()
endif()

install(TARGETS pva_dualcam_cpp RUNTIME DESTINATION .)
