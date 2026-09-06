cmake_minimum_required(VERSION 3.16)
project(PaperVision_System LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)

# Pylon Paths
set(PYLON_ROOT /opt/pylon)
include_directories(${PYLON_ROOT}/include)
link_directories(${PYLON_ROOT}/lib)

# Find Packages
find_package(OpenCV REQUIRED)
find_package(Qt5 COMPONENTS Core Widgets Gui Concurrent Svg Network OpcUa REQUIRED)
find_package(X11 REQUIRED)

# Defines BUILD_TESTING and registers tests with CTest when enabled.
include(CTest)

# Init Sources (shared by the app and the regression-test executable; main.cpp
# is added separately below so the test has its own main()).
set(SOURCES
src/core/CameraManager.cpp
src/core/EventController.cpp
src/core/EventDatabase.cpp
src/core/SpeedProfile.cpp
src/core/VideoStreamReader.cpp
src/processing/ImageBuffer.cpp
src/processing/DefectDetector.cpp
src/processing/EventSignalScanner.cpp
src/processing/HistogramAnalyzer.cpp
src/processing/VideoEncoder.cpp
src/gui/MainWindow.cpp
src/gui/LiveDashboard.cpp
src/gui/AnalysisView.cpp
src/gui/DetailView.cpp
src/gui/widgets/CameraWidget.cpp
src/gui/widgets/AnalysisVideoWidget.cpp
src/gui/widgets/EventDashboard.cpp
src/gui/ConfigDialog.cpp
src/config/CameraConfig.cpp
src/communication/OpcUaClientService.cpp
src/gui/widgets/ToggleSwitch.cpp
src/gui/widgets/IconManager.cpp
src/gui/widgets/CameraCard.cpp
src/gui/widgets/CameraDeviceSettingsDialog.cpp
src/gui/widgets/NetworkSummaryHeader.cpp
src/gui/widgets/DeleteConfirmationDialog.cpp
src/gui/widgets/IpConfiguratorPanel.cpp
src/gui/widgets/FixedIpListPanel.cpp
src/gui/widgets/MachineGroupsPanel.cpp
src/gui/widgets/MachineLayoutPanel.cpp
src/gui/widgets/DocsDialog.cpp
)

# Header files for Qt MOC
set(HEADERS
src/gui/MainWindow.h
src/gui/LiveDashboard.h
src/gui/AnalysisView.h
src/gui/DetailView.h
src/gui/ConfigDialog.h
src/gui/widgets/CameraWidget.h
src/gui/widgets/AnalysisVideoWidget.h
src/gui/widgets/EventDashboard.h
src/gui/widgets/ToggleSwitch.h
src/gui/widgets/IconManager.h
src/gui/widgets/CameraCard.h
src/gui/widgets/CameraDeviceSettingsDialog.h
src/gui/widgets/NetworkSummaryHeader.h
src/gui/widgets/DeleteConfirmationDialog.h
src/gui/widgets/IpConfiguratorPanel.h
src/gui/widgets/FixedIpListPanel.h
src/gui/widgets/MachineGroupsPanel.h
src/gui/widgets/MachineLayoutPanel.h
src/gui/widgets/DocsDialog.h
src/communication/OpcUaClientService.h
src/communication/OpcUaRuntimeStatus.h
src/processing/EventSignalScanner.h
)

# Include Directories
include_directories(
    src
    ${OpenCV_INCLUDE_DIRS}
)

# Qt Mocking
qt5_wrap_cpp(MOC_SOURCES ${HEADERS})
qt5_add_resources(RESOURCES resources.qrc)

# Executable
add_executable(PaperVision_App src/main.cpp ${SOURCES} ${MOC_SOURCES} ${RESOURCES})

# Link Libraries
target_link_libraries(PaperVision_App
    ${OpenCV_LIBS}
    Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Concurrent Qt5::Svg Qt5::Network Qt5::OpcUa
    X11::X11
    pthread
    pylonbase
    pylonutility
    GenApi_gcc_v3_1_Basler_pylon
    GCBase_gcc_v3_1_Basler_pylon
)

# ---------------------------------------------------------------------------
# Regression tests
#
# PaperVision_RegressionTest exercises ConfigDialog entry paths (tab showEvent
# auto-discovery and the Detect Server button) against a deliberately silent
# OPC UA endpoint and asserts they never block on the network probe.
# Run with: ctest -R config_dialog_network_regression
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    find_package(Qt5 COMPONENTS Test REQUIRED)

    qt5_wrap_cpp(TEST_MOC_SOURCES tests/ConfigDialogNetworkRegression.h)

    add_executable(PaperVision_RegressionTest
        ${SOURCES}
        tests/ConfigDialogNetworkRegression.cpp
        ${TEST_MOC_SOURCES}
        ${MOC_SOURCES}
        ${RESOURCES}
    )

    target_link_libraries(PaperVision_RegressionTest
        ${OpenCV_LIBS}
        Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Concurrent Qt5::Svg Qt5::Network Qt5::OpcUa Qt5::Test
        X11::X11
        pthread
        pylonbase
        pylonutility
        GenApi_gcc_v3_1_Basler_pylon
        GCBase_gcc_v3_1_Basler_pylon
    )

    add_test(NAME config_dialog_network_regression COMMAND PaperVision_RegressionTest)
    set_tests_properties(config_dialog_network_regression PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PYLON_CAMEMU=2")
endif()
