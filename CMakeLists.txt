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
src/gui/widgets/LiveViewWindow.cpp
src/gui/widgets/AnalysisVideoWidget.cpp
src/gui/widgets/EventDashboard.cpp
src/gui/ConfigDialog.cpp
src/config/CameraConfig.cpp
src/config/AppPaths.cpp
src/config/OpcUaValidation.cpp
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
src/gui/widgets/OpcUaLiveStatusWindow.cpp
src/gui/widgets/DocsDialog.cpp
src/gui/widgets/ChangelogDialog.cpp
src/gui/widgets/CircularSpinner.cpp
src/gui/widgets/AccessibleTextSummary.cpp
)

# Header files for Qt MOC
set(HEADERS
src/gui/MainWindow.h
src/gui/LiveDashboard.h
src/gui/AnalysisView.h
src/gui/DetailView.h
src/gui/ConfigDialog.h
src/gui/widgets/CameraWidget.h
src/gui/widgets/LiveViewWindow.h
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
src/gui/widgets/OpcUaLiveStatusWindow.h
src/gui/widgets/DocsDialog.h
src/gui/widgets/ChangelogDialog.h
src/gui/widgets/CircularSpinner.h
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
# OPC UA endpoint and asserts they never block on the network probe, plus the
# event recorder's cross-camera timing (a camera that misses frames must still
# record the same post-trigger window in seconds, and a camera that stops
# streaming must be recorded as missing) and its section-scoped recording (a
# narrowed trigger records only its sections, and a selection with no camera is
# refused with a reason), and the in-app changelog (the bundled CHANGELOG.md
# parses into one row per release and renders as themed HTML). All suites drive
# the real classes with synthetic frame clocks — no cameras are involved.
# Run with: ctest -R config_dialog_network_regression
#           ctest -R event_capture_timing
#           ctest -R trigger_record_scope
#           ctest -R analysis_review_tiles
#           ctest -R changelog_dialog_render
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    find_package(Qt5 COMPONENTS Test REQUIRED)

    qt5_wrap_cpp(TEST_MOC_SOURCES
        tests/ConfigDialogNetworkRegression.h
        tests/EventCaptureTiming.h
        tests/TriggerRecordScope.h
        tests/AnalysisReviewTiles.h
        tests/ChangelogDialogTest.h
        tests/DefectSeverityClassification.h
        tests/AccessibilitySummary.h
        tests/UiTypeScaleDefaults.h
        tests/OpcUaSpeedAnchorValidation.h
        tests/AboutDialogTest.h
    )

    add_executable(PaperVision_RegressionTest
        ${SOURCES}
        tests/ConfigDialogNetworkRegression.cpp
        tests/EventCaptureTiming.cpp
        tests/TriggerRecordScope.cpp
        tests/AnalysisReviewTiles.cpp
        tests/ChangelogDialogTest.cpp
        tests/DefectSeverityClassification.cpp
        tests/AccessibilitySummary.cpp
        tests/UiTypeScaleDefaults.cpp
        tests/OpcUaSpeedAnchorValidation.cpp
        tests/AboutDialogTest.cpp
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

    add_test(NAME config_dialog_network_regression COMMAND PaperVision_RegressionTest config)
    add_test(NAME event_capture_timing COMMAND PaperVision_RegressionTest capture)
    add_test(NAME trigger_record_scope COMMAND PaperVision_RegressionTest scope)
    add_test(NAME analysis_review_tiles COMMAND PaperVision_RegressionTest tiles)
    add_test(NAME changelog_dialog_render COMMAND PaperVision_RegressionTest changelog)
    add_test(NAME defect_severity_classification COMMAND PaperVision_RegressionTest severity)
    add_test(NAME accessibility_summaries COMMAND PaperVision_RegressionTest a11y)
    add_test(NAME ui_type_scale COMMAND PaperVision_RegressionTest type)
    add_test(NAME opcua_speed_anchor_validation COMMAND PaperVision_RegressionTest anchors)
    add_test(NAME about_pylon_version COMMAND PaperVision_RegressionTest about)
    set_tests_properties(config_dialog_network_regression event_capture_timing
        trigger_record_scope analysis_review_tiles changelog_dialog_render
        defect_severity_classification accessibility_summaries ui_type_scale
        opcua_speed_anchor_validation about_pylon_version
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PYLON_CAMEMU=2")
endif()

# ---------------------------------------------------------------------------
# Native packaging - Debian .deb for Ubuntu 22.04 (jammy)
#
# Off by default, so the Docker image keeps building without install rules.
# packaging/build-deb.sh (also used by the release workflow) enables it and
# CPack turns the rules below into papervision_<version>_amd64.deb.
#
# QtOpcUa and open62541 are not in the Ubuntu archive, so the package bundles
# the copies built on the packaging host (Qt plugins reject build-version
# mismatches, hence the target-distro build). Basler pylon stays a documented
# vendor prerequisite: it is proprietary and is never redistributed here.
# ---------------------------------------------------------------------------
option(PAPERVISION_PACKAGING "Create install rules and CPack .deb configuration" OFF)

if(PAPERVISION_PACKAGING)
    # The payload mixes absolute roots (/opt/papervision, /usr), so the
    # install prefix must be the filesystem root.
    if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/")
        message(FATAL_ERROR
            "PAPERVISION_PACKAGING installs into /opt/papervision and /usr - "
            "configure with -DCMAKE_INSTALL_PREFIX=/ (packaging/build-deb.sh does this)")
    endif()

    set(PAPERVISION_QTOPUCA_LIB_DIR "/usr/lib/x86_64-linux-gnu" CACHE PATH
        "Directory holding the built libQt5OpcUa shared library")
    set(PAPERVISION_QTOPUCA_PLUGIN_DIR "/usr/lib/x86_64-linux-gnu/qt5/plugins/opcua" CACHE PATH
        "Directory holding the built QtOpcUa open62541 backend plugin")
    set(PAPERVISION_OPEN62541_LIB_DIR "/usr/local/lib" CACHE PATH
        "Directory holding the built libopen62541 shared library")

    install(TARGETS PaperVision_App RUNTIME DESTINATION opt/papervision/bin)
    # Fallback for direct invocations; the /usr/bin/papervision launcher also
    # exports these paths (needed for the Qt plugin directory).
    set_target_properties(PaperVision_App PROPERTIES
        INSTALL_RPATH "/opt/papervision/lib;/opt/pylon/lib")

    # The globs pick up the versioned library together with its SONAME links;
    # CPack keeps them as symlinks in the payload. Fail loudly when the bundled
    # libraries were not built.
    file(GLOB QTOPUCA_LIBS "${PAPERVISION_QTOPUCA_LIB_DIR}/libQt5OpcUa.so.5*")
    if(NOT QTOPUCA_LIBS)
        message(FATAL_ERROR
            "libQt5OpcUa.so.5* not found in ${PAPERVISION_QTOPUCA_LIB_DIR} - build qtopcua first")
    endif()
    install(FILES ${QTOPUCA_LIBS} DESTINATION opt/papervision/lib)

    file(GLOB OPEN62541_LIBS "${PAPERVISION_OPEN62541_LIB_DIR}/libopen62541.so.1*")
    if(NOT OPEN62541_LIBS)
        message(FATAL_ERROR
            "libopen62541.so.1* not found in ${PAPERVISION_OPEN62541_LIB_DIR} - build open62541 first")
    endif()
    install(FILES ${OPEN62541_LIBS} DESTINATION opt/papervision/lib)

    # The plugin file name has changed across QtOpcUa releases
    # (libqtopen62541backend.so, libopen62541_backend.so), so take whatever the
    # built backend provides.
    file(GLOB OPCUA_BACKEND_PLUGINS "${PAPERVISION_QTOPUCA_PLUGIN_DIR}/*.so")
    if(NOT OPCUA_BACKEND_PLUGINS)
        message(FATAL_ERROR
            "no QtOpcUa backend plugin (*.so) found in ${PAPERVISION_QTOPUCA_PLUGIN_DIR} - build qtopcua first")
    endif()
    install(FILES ${OPCUA_BACKEND_PLUGINS} DESTINATION opt/papervision/plugins/opcua)

    install(PROGRAMS packaging/deb/papervision-wrapper.sh DESTINATION usr/bin RENAME papervision)
    install(FILES packaging/deb/papervision.desktop DESTINATION usr/share/applications)
    install(FILES packaging/deb/papervision.svg DESTINATION usr/share/icons/hicolor/scalable/apps)
    install(FILES packaging/deb/99-papervision.conf DESTINATION usr/lib/sysctl.d)
    install(PROGRAMS packaging/deb/setup-network.sh DESTINATION usr/lib/papervision)
    install(FILES packaging/deb/copyright DESTINATION usr/share/doc/papervision)
    install(FILES docs/installation.md DESTINATION usr/share/doc/papervision RENAME INSTALL.md)

    # --- CPack ---
    file(STRINGS "${CMAKE_SOURCE_DIR}/src/config/AppVersion.h" _app_version_line
         REGEX "kNumber")
    string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ignored "${_app_version_line}")
    set(PAPERVISION_VERSION "${CMAKE_MATCH_1}")

    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "papervision")
    set(CPACK_PACKAGE_VENDOR "PaperVision")
    set(CPACK_PACKAGE_CONTACT "PaperVision maintainers <xlibraboy@users.noreply.github.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Paper machine surface inspection: live Basler GigE camera monitoring and event review")
    set(CPACK_PACKAGE_VERSION "${PAPERVISION_VERSION}")
    set(CPACK_DEBIAN_PACKAGE_VERSION "${PAPERVISION_VERSION}-1")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/xlibraboy/web_vision_pro")
    # Explicit runtime dependencies (mirrors the known-good Docker image, with
    # jammy package names). Keep in sync with .docker/Dockerfile; the release
    # checklist verifies them with dpkg-shlibdeps before publishing.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt5core5a, libqt5gui5, libqt5widgets5, libqt5network5, libqt5concurrent5, libqt5svg5, libopencv-core4.5d, libopencv-imgproc4.5d, libopencv-imgcodecs4.5d, libopencv-videoio4.5d, libx11-6, libgl1, libxkbcommon-x11-0, libxcb1, libxcb-xinerama0, libxcb-keysyms1, libxcb-icccm4, libxcb-image0, libxcb-shm0, libxcb-util1, libxcb-render-util0, libxcb-randr0, libxcb-shape0, libxcb-sync1, libxcb-xfixes0, libxcb-xinput0, fonts-noto-core")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_SOURCE_DIR}/packaging/deb/postrm")
    set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    # Payload paths start at the filesystem root (see the prefix check above).
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    # Keep the symbol table: the fatal-signal handler prints backtraces that
    # must stay resolvable in the field.
    set(CPACK_STRIP_FILES FALSE)

    include(CPack)
endif()
