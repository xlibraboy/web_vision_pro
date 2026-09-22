#ifndef BUFFER_FACTORY_H
#define BUFFER_FACTORY_H

// Include files to use the pylon API.
#include <pylon/PylonIncludes.h>
// Namespace for using pylon objects.
using namespace Pylon;

// Namespace for using cout.
using namespace std;

// A user-provided buffer factory for grabbing into CUDA GPU memory.
class CudaBufferFactory : public IBufferFactory
{
public:
    CudaBufferFactory(int width, int height, int type, cv::cuda::HostMem::AllocType alloc_type):
          m_width(width),           // Width of image
          m_height(height),         // Height of image
          m_element_type(type),     // OpenCV pixel format of the raw Bayer image, e.g., CV_8UC1 (8 bit per pixel, unsigned, one color channel)
          m_alloc_type(alloc_type), // CUDA allocation type, cuda::HostMem::SHARED -> shared GPU memory
          m_dummy(1,1,type)         // dummy OpenCV Mat used to check whether the m_element_type, width, and height fit to the buffer size
    {
    }

    virtual ~CudaBufferFactory()
    {
    }


    // Will be called when the Instant Camera object needs to allocate a buffer.
    // Return the buffer and context data in the output parameters.
    // Warning: This method can be called by different threads.
    virtual void AllocateBuffer( size_t bufferSize, void** pCreatedBuffer, intptr_t& bufferContext)
    {
        try {
        	if(m_dummy.elemSize() * m_width * m_height < bufferSize) {
        		cout << "Buffer Factory configuration doesn't fit the requested buffer. No buffer created." << endl;
        		return;
        	}

        	cv::cuda::HostMem* m = new cv::cuda::HostMem(m_height, m_width, m_element_type, m_alloc_type);
        	if(!m->isContinuous()) {
        		cout << "HostMem is not continuous. This will damage the image." << endl;
        	}

            m->step = m_width;
        	cv::Mat cpuMat = m->createMatHeader();
           	*pCreatedBuffer = cpuMat.data;
            bufferContext = (intptr_t)(void*)m;
        }
        catch (const std::exception&) {
            throw;
        }
    }


    // Frees a previously allocated buffer.
    // Warning: This method can be called by different threads.
    virtual void FreeBuffer( void* pCreatedBuffer, intptr_t bufferContext)
    {
    	cv::cuda::HostMem* m = (cv::cuda::HostMem*)bufferContext;
    	delete m;
    }


    // Destroys the buffer factory.
    // This will be used when you pass the ownership of the buffer factory instance to pylon
    // by defining Cleanup_Delete. pylon will call this function to destroy the instance
    // of the buffer factory. If you don't pass the ownership to pylon (Cleanup_None),
    // this method will be ignored.
    virtual void DestroyBufferFactory()
    {
        delete this;
    }


protected:

    cv::cuda::HostMem::AllocType m_alloc_type;
    int m_width;
    int m_height;
    int m_element_type;
    cv::Mat m_dummy;

}; // End of CudaBufferFactory class

#endif // BUFFER_FACTORY_H
