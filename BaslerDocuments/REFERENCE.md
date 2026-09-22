# Basler Reference Index

Use this folder as the local Basler/Pylon reference for `web_vision_pro`.

## Documents

### Converted text (easy to search/read)
- `converted_text/scout_GigE_User_Manual.txt`
- `converted_text/pylon_SDK_Samples_Manual.txt`
- `converted_text/How_to_Use_OpenCV.txt`
- `converted_text/Basler_ApplicationNote.txt`
- `converted_text/Basler_WhitePaper.txt`

### Original PDFs
- `AW00011919000_scout_GigE_User_Manual.pdf`
- `AW00138003000_Application_Note_Monitoring_the_housing_temperature_of_Basler_ace_cameras.pdf`
- `AW00176301000_How_to_Use_OpenCV_and_CUDA_in_pylon_Applications_1/How_to_Use_OpenCV_and_CUDA_in_pylon_Applications.pdf`
- `Basler_ApplicationNote.pdf`
- `Basler_WhitePaper.pdf`

## C++ Sample Code

### Camera grabbing
- `C++/Grab/Grab.cpp`
- `C++/Grab_MultipleCameras/Grab_MultipleCameras.cpp`
- `C++/Grab_Strategies/Grab_Strategies.cpp`
- `C++/Grab_UsingGrabLoopThread/Grab_UsingGrabLoopThread.cpp`
- `C++/Grab_CameraEvents/Grab_CameraEvents.cpp`
- `C++/Grab_ChunkImage/Grab_ChunkImage.cpp`
- `C++/Grab_UsingExposureEndEvent/Grab_UsingExposureEndEvent.cpp`
- `C++/Grab_UsingActionCommand/Grab_UsingActionCommand.cpp`
- `C++/Grab_MultiCast/Grab_MultiCast.cpp`

### Camera configuration / parameters
- `C++/ParametrizeCamera_NativeParameterAccess/ParametrizeCamera_NativeParameterAccess.cpp`
- `C++/ParametrizeCamera_GenericParameterAccess/ParametrizeCamera_GenericParameterAccess.cpp`
- `C++/ParametrizeCamera_Configurations/ParametrizeCamera_Configurations.cpp`
- `C++/ParametrizeCamera_AutoFunctions/ParametrizeCamera_AutoFunctions.cpp`
- `C++/ParametrizeCamera_LoadAndSave/ParametrizeCamera_LoadAndSave.cpp`
- `C++/ParametrizeCamera_UserSets/ParametrizeCamera_UserSets.cpp`
- `C++/ParametrizeCamera_LookupTable/ParametrizeCamera_LookupTable.cpp`
- `C++/ParametrizeCamera_Shading/ParametrizeCamera_Shading.cpp`
- `C++/ParametrizeCamera_SerialCommunication/ParametrizeCamera_SerialCommunication.cpp`

### Utility / image processing
- `C++/Utility_Image/Utility_Image.cpp`
- `C++/Utility_ImageFormatConverter/Utility_ImageFormatConverter.cpp`
- `C++/Utility_ImageLoadAndSave/Utility_ImageLoadAndSave.cpp`
- `C++/Utility_ImageDecompressor/Utility_ImageDecompressor.cpp`
- `C++/Utility_GrabVideo/Utility_GrabVideo.cpp`
- `C++/Utility_InstantInterface/Utility_InstantInterface.cpp`
- `C++/Utility_IpConfig/Utility_IpConfig.cpp`

### Reliability / device handling
- `C++/DeviceRemovalHandling/DeviceRemovalHandling.cpp`

### Shared sample headers
- `C++/include/CameraEventPrinter.h`
- `C++/include/ConfigurationEventPrinter.h`
- `C++/include/ImageEventPrinter.h`
- `C++/include/PixelFormatAndAoiConfiguration.h`
- `C++/include/SampleImageCreator.h`

## Recommended references for this app

For a Qt/OpenCV/Pylon paper inspection system, start with:
1. `converted_text/scout_GigE_User_Manual.txt`
2. `converted_text/pylon_SDK_Samples_Manual.txt`
3. `converted_text/How_to_Use_OpenCV.txt`
4. `C++/Grab/Grab.cpp`
5. `C++/Grab_MultipleCameras/Grab_MultipleCameras.cpp`
6. `C++/Grab_UsingGrabLoopThread/Grab_UsingGrabLoopThread.cpp`
7. `C++/ParametrizeCamera_Configurations/ParametrizeCamera_Configurations.cpp`
8. `C++/Utility_ImageFormatConverter/Utility_ImageFormatConverter.cpp`
9. `C++/DeviceRemovalHandling/DeviceRemovalHandling.cpp`

## How I can use this

I can use these files as the implementation reference when working on:
- camera enumeration and connection
- continuous grabbing
- multi-camera handling
- trigger/event handling
- pixel format and image conversion
- OpenCV integration
- reconnect/device removal logic
- parameter configuration and user sets

When you ask for a feature, I can map it to the most relevant sample(s) in this folder and adapt the logic into `src/core`, `src/gui`, or `src/processing`.
