// Reproduces the exact pylon write sequence CameraManager performs on the
// device-dialog apply path (post-fix): Enable=true twice, rate writes, then
// an exposure write — all while grabbing. Prints each step; a SIGSEGV in a
// GenApi write shows up as a stop right after the last flushed step.
#include <pylon/PylonIncludes.h>
#include <iostream>

using namespace Pylon;

int main() {
    PylonInitialize();
    try {
        CTlFactory& tl = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        if (tl.EnumerateDevices(devices) == 0) {
            std::cout << "no devices" << std::endl;
            return 1;
        }
        CInstantCamera cam(tl.CreateDevice(devices[0]));
        cam.Open();
        std::cout << "opened: "
                  << cam.GetDeviceInfo().GetModelName().c_str() << std::endl;
        GenApi::INodeMap& nm = cam.GetNodeMap();

        cam.StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByUser);
        std::cout << "grabbing" << std::endl;

        GenApi::CBooleanPtr enable(nm.GetNode("AcquisitionFrameRateEnable"));
        std::cout << "enable node: " << (enable ? "present" : "NULL")
                  << " writable=" << (enable && GenApi::IsWritable(enable))
                  << " value=" << (enable && GenApi::IsReadable(enable) ? enable->GetValue() : -1)
                  << std::endl;

        if (enable && GenApi::IsWritable(enable)) {
            enable->SetValue(true);
            std::cout << "enable->SetValue(true) #1 OK" << std::endl;
            enable->SetValue(true);  // unchanged write — the suspicious one
            std::cout << "enable->SetValue(true) #2 (unchanged) OK" << std::endl;
        }

        GenApi::CFloatPtr fps(nm.GetNode("AcquisitionFrameRateAbs"));
        if (!fps || !GenApi::IsWritable(fps)) fps = GenApi::CFloatPtr(nm.GetNode("AcquisitionFrameRate"));
        if (fps && GenApi::IsWritable(fps)) {
            std::cout << "fps node min=" << fps->GetMin() << " max=" << fps->GetMax() << std::endl;
            fps->SetValue(50.0);
            std::cout << "fps->SetValue(50) #1 OK" << std::endl;
            fps->SetValue(50.0);  // unchanged write
            std::cout << "fps->SetValue(50) #2 (unchanged) OK" << std::endl;
        } else {
            std::cout << "no writable fps node" << std::endl;
        }

        GenApi::CFloatPtr exp(nm.GetNode("ExposureTimeAbs"));
        if (!exp || !GenApi::IsWritable(exp)) exp = GenApi::CFloatPtr(nm.GetNode("ExposureTime"));
        if (exp && GenApi::IsWritable(exp)) {
            exp->SetValue(12000.0);
            std::cout << "exposure->SetValue(12000) OK" << std::endl;
        }

        GenApi::CFloatPtr res(nm.GetNode("ResultingFrameRate"));
        if (!res || !GenApi::IsReadable(res)) res = GenApi::CFloatPtr(nm.GetNode("ResultingFrameRateAbs"));
        std::cout << "ResultingFrameRate=" << (res && GenApi::IsReadable(res) ? res->GetValue() : -1.0) << std::endl;

        cam.StopGrabbing();
        cam.Close();
        std::cout << "ALL OK" << std::endl;
    } catch (const GenericException& e) {
        std::cout << "GENAPI EXCEPTION: " << e.GetDescription() << std::endl;
        return 2;
    }
    return 0;
}

