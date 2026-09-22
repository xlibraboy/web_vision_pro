// Grab-Debayer-Zerocopy.cpp
/*
    Note: Before getting started, Basler recommends reading the "Programmer's Guide" topic
    in the pylon C++ API documentation: https://docs.baslerweb.com/pylonapi/cpp/
    If you are upgrading to a higher major version of pylon, Basler also
    strongly recommends reading the "Migrating from Previous Versions" topic in the pylon C++ API documentation.

    This sample illustrates how to demosaic a raw Bayer image 
    - with the CPU (un-define USE_ZEROCOPY_AND_CUDA_DEBAYERING), or 
    - with GPU and direct grabbing into the shared GPU/CPU memory (zerocopy) using a buffer factory 
      (define USE_ZEROCOPY_AND_CUDA_DEBAYERING)
*/

#define USE_ZEROCOPY_AND_CUDA_DEBAYERING
#define WITH_OPENGL


// Include files to use the pylon API.
#include <pylon/PylonIncludes.h>
#ifdef PYLON_WIN_BUILD
#    include <pylon/PylonGUI.h>
#endif

#include <opencv2/highgui.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp>
#include <cuda_runtime.h>

#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
    #include "cuda_buffer_factory.h"
#endif

// Namespace for using pylon objects.
using namespace Pylon;
using namespace GenApi;

// Namespace for using cout.
using namespace std;

// Namespace for using opencv.
using namespace cv;

#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
    #define WINDOW_TITLE "Zerocopy and Demosaicing in GPU"
#else
    #define WINDOW_TITLE "Demosaicing in CPU"
#endif



int main(int argc, char* argv[])
{
    // The exit code of the sample application.
    int exitCode = 0;

    // Before using any pylon methods, the pylon runtime must be initialized. 
    PylonInitialize();

    try
    {
        // Create an instant camera object with the camera device found first.
        CInstantCamera camera( CTlFactory::GetInstance().CreateFirstDevice());
	    camera.Open();

        // Print the model name of the camera.
        cout << "Using device " << camera.GetDeviceInfo().GetModelName() << endl;

        // Use larger USB transfer size (if possible)
        INodeMap& sg_nodemap = camera.GetStreamGrabberNodeMap();
        if (CIntegerParameter (sg_nodemap, "MaxTransferSize").IsReadable())
        {
            CIntegerParameter (sg_nodemap, "MaxTransferSize").TrySetToMaximum();
            cout << "USB Transfer Size is set to " << CIntegerParameter (sg_nodemap, "MaxTransferSize").GetValue() << endl;
        }

        INodeMap& nodemap = camera.GetNodeMap();

        // Set camera pixel format to raw Bayer
        if (CEnumParameter (nodemap, "PixelFormat").TrySetValue("BayerGB8"))
        {
            cout << "Camera PixelFormat set to BayerGB8" << endl;
        }
        else
        {
            cout << "Camera does not support BayerGB8 pixel format, terminating program" << endl;
            goto endprogramm;
        }

        // Retrieve image size
        int width = CIntegerParameter (nodemap, "Width").GetValue();
        int height = CIntegerParameter (nodemap, "Height").GetValue();

        // The MaxNumBuffer parameter can be used to control the count of buffers
        // allocated for grabbing. The default value of this parameter is 10.
        camera.MaxNumBuffer = 5;

#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
        CudaBufferFactory* factory = new CudaBufferFactory(width, height, CV_8UC1, cuda::HostMem::SHARED);
        camera.SetBufferFactory(factory, Cleanup_Delete);
#endif

        // Start grabbing
        camera.StartGrabbing(GrabStrategy_LatestImages);

        // This smart pointer will receive the grab result data.
        CGrabResultPtr ptrGrabResult;

        // Create a GUI window for displaying images with OpenGL support.
#ifdef WITH_OPENGL
        namedWindow(WINDOW_TITLE, WINDOW_OPENGL);
#else
        namedWindow(WINDOW_TITLE, WINDOW_NORMAL);
#endif

#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
    #ifndef WITH_OPENGL
        // CUDA GPU memory without OpenGL requires download from cuda::GpuMat to cv::Mat
        Mat mat_display;
    #endif
#endif

        // Matrices for the raw Bayer and for the converted color BGR image
#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
        cuda::GpuMat mat_color;
        cuda::GpuMat mat_raw;
#else
		Mat mat_color;
        Mat mat_raw;
#endif

        cout << "Press Escape to terminate program" << endl;

        while ( camera.IsGrabbing())
        {
            // Wait for an image and then retrieve it. A timeout of 5000 ms is used.
            camera.RetrieveResult( 5000, ptrGrabResult, TimeoutHandling_ThrowException);

            // Image grabbed successfully?
            if (ptrGrabResult->GrabSucceeded())
            {
#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
                // Image has been grabbed directly into GPU memory (provided by the buffer factory)
                cuda::HostMem* cmem = (cuda::HostMem*)ptrGrabResult->GetBufferContext();
                mat_raw = cmem->createGpuMatHeader();
                cuda::demosaicing (mat_raw, mat_color, COLOR_BayerGB2BGR);
#else
                // Demosaicing is done in CPU
                mat_raw = Mat(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(),CV_8UC1, ptrGrabResult->GetBuffer());
                cvtColor(mat_raw, mat_color, COLOR_BayerGB2RGB);
#endif
            }
            else
            {
                cout << "Error: " << ptrGrabResult->GetErrorCode() << " " << ptrGrabResult->GetErrorDescription() << endl;
                continue;
            }

#ifdef USE_ZEROCOPY_AND_CUDA_DEBAYERING
    #ifndef WITH_OPENGL
            mat_color.download(mat_display);
            imshow(WINDOW_TITLE, mat_display);
    #else
            imshow(WINDOW_TITLE, mat_color);
    #endif
#else
            imshow(WINDOW_TITLE, mat_color);
#endif
            int key = waitKey(1);
            if (key == 27) // ESC key was pressed
            {
                camera.StopGrabbing();
                goto endprogramm;
            }
        }
    }
    catch (const GenericException &e)
    {
        // Error handling
        cerr << "An exception occurred." << endl
        << e.GetDescription() << endl;
        exitCode = 1;
    }

endprogramm:
    // Releases all pylon resources.
    PylonTerminate();  

    return exitCode;
}
