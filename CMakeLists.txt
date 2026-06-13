cmake_minimum_required(VERSION 3.16)
project(PaperVision_System LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

set(OPEN62541_DIR ${CMAKE_BINARY_DIR}/third_party/open62541)
set(OPEN62541_HEADER ${OPEN62541_DIR}/open62541.h)
set(OPEN62541_SOURCE ${OPEN62541_DIR}/open62541.c)
file(MAKE_DIRECTORY ${OPEN62541_DIR})

if(NOT EXISTS ${OPEN62541_HEADER})
    file(DOWNLOAD
        https://github.com/open62541/open62541/releases/download/v1.5.4/open62541.h
        ${OPEN62541_HEADER}
        STATUS OPEN62541_HEADER_STATUS
        SHOW_PROGRESS
    )
    list(GET OPEN62541_HEADER_STATUS 0 OPEN62541_HEADER_CODE)
    list(GET OPEN62541_HEADER_STATUS 1 OPEN62541_HEADER_MESSAGE)
    if(NOT OPEN62541_HEADER_CODE EQUAL 0)
        message(FATAL_ERROR \"Failed to download open62541.h: ${OPEN62541_HEADER_MESSAGE}\")
    endif()
endif()

if(NOT EXISTS ${OPEN62541_SOURCE})
    file(DOWNLOAD
        https://github.com/open62541/open62541/releases/download/v1.5.4/open62541.c
        ${OPEN62541_SOURCE}
        STATUS OPEN62541_SOURCE_STATUS
        SHOW_PROGRESS
    )
    list(GET OPEN62541_SOURCE_STATUS 0 OPEN62541_SOURCE_CODE)
    list(GET OPEN62541_SOURCE_STATUS 1 OPEN62541_SOURCE_MESSAGE)
    if(NOT OPEN62541_SOURCE_CODE EQUAL 0)
        message(FATAL_ERROR \"Failed to download open62541.c: ${OPEN62541_SOURCE_MESSAGE}\")
    endif()
endif()
file(READ ${OPEN62541_HEADER} OPEN62541_HEADER_CONTENT)
string(REPLACE
    "#define UA_ENABLE_ENCRYPTION_MBEDTLS"
    "/* #undef UA_ENABLE_ENCRYPTION_MBEDTLS */"
    OPEN62541_HEADER_CONTENT
    "${OPEN62541_HEADER_CONTENT}"
)
file(WRITE ${OPEN62541_HEADER} "${OPEN62541_HEADER_CONTENT}")



# Pylon Paths
set(PYLON_ROOT /opt/pylon)
include_directories(${PYLON_ROOT}/include)
link_directories(${PYLON_ROOT}/lib)

# Find Packages
find_package(OpenCV REQUIRED)
find_package(Qt5 COMPONENTS Core Widgets Gui Concurrent Svg Network REQUIRED)

# Init Sources
set(SOURCES
src/main.cpp
src/core/CameraManager.cpp
src/core/EventController.cpp
src/core/EventDatabase.cpp
src/core/VideoStreamReader.cpp
src/processing/ImageBuffer.cpp
src/processing/DefectDetector.cpp
src/processing/VideoEncoder.cpp
src/gui/MainWindow.cpp
src/gui/LiveDashboard.cpp
src/gui/AnalysisView.cpp
src/gui/DetailView.cpp
src/gui/widgets/CameraWidget.cpp
src/gui/widgets/AnalysisVideoWidget.cpp
src/gui/ConfigDialog.cpp
src/config/CameraConfig.cpp
src/communication/OpcUaClientService.cpp
src/gui/widgets/ToggleSwitch.cpp
src/gui/widgets/IconManager.cpp
src/gui/widgets/CameraCard.cpp
src/gui/widgets/CameraDeviceSettingsDialog.cpp
src/gui/widgets/NetworkSummaryHeader.cpp
src/gui/widgets/DeleteConfirmationDialog.cpp
${OPEN62541_SOURCE}
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
src/gui/widgets/ToggleSwitch.h
src/gui/widgets/IconManager.h
src/gui/widgets/CameraCard.h
src/gui/widgets/CameraDeviceSettingsDialog.h
src/gui/widgets/NetworkSummaryHeader.h
src/gui/widgets/DeleteConfirmationDialog.h
src/communication/OpcUaClientService.h
)

# Include Directories
include_directories(
    src
    ${OpenCV_INCLUDE_DIRS}
    ${OPEN62541_DIR}
)

# Qt Mocking
qt5_wrap_cpp(MOC_SOURCES ${HEADERS})
qt5_add_resources(RESOURCES resources.qrc)

# Executable
add_executable(PaperVision_App ${SOURCES} ${MOC_SOURCES} ${RESOURCES})

# Link Libraries
target_link_libraries(PaperVision_App
    ${OpenCV_LIBS}
    Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Concurrent Qt5::Svg Qt5::Network
    pthread
    pylonbase
    pylonutility
    GenApi_gcc_v3_1_Basler_pylon
    GCBase_gcc_v3_1_Basler_pylon
)
# If not mocking, would link Pylon here:
# target_link_libraries(PaperVision_App ${Pylon_LIBRARIES})
