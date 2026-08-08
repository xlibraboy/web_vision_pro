#include "CameraManager.h"
#include "../config/CameraConfig.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <memory>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <pylon/gige/GigETransportLayer.h>
#include <pylon/gige/BaslerGigEInstantCamera.h>
#include <QDateTime>
#include <QDir>

// Use Pylon namespace
using namespace Pylon;

namespace {
std::string normalizeMacAddress(const std::string& mac) {
    std::string normalized;
    normalized.reserve(mac.size());
    for (unsigned char ch : mac) {
        if (std::isxdigit(ch)) {
            normalized.push_back(static_cast<char>(std::toupper(ch)));
        }
    }
    return normalized;
}

// ---------------------------------------------------------------------------
// Temporary link-local ("assign temporary IP") bridge
//
// A camera stuck in AutoIP mode (current IP 169.254.x.x) is not reachable
// from the host subnet. The scA780 family ignores the GVCP force-IP
// broadcast (IGigETransportLayer::BroadcastIpConfiguration returns false),
// so the only working way to reach such a camera is to give the host NIC a
// temporary link-local address, which makes the camera's AutoIP address
// routable. The address is added as a SECONDARY address (netlink), so the
// interface's primary address is never touched, and it is removed again as
// soon as the IP write completes.
// ---------------------------------------------------------------------------

// Adds or removes a secondary IPv4 address on an interface without touching
// the primary address. Returns true on success.
bool setSecondaryIpv4Address(int ifindex, const std::string& addr, int prefixLen, bool add) {
    struct {
        struct nlmsghdr n;
        struct ifaddrmsg i;
        char buf[128];
    } req = {};
    req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | (add ? (NLM_F_CREATE | NLM_F_EXCL) : 0);
    req.n.nlmsg_type = add ? RTM_NEWADDR : RTM_DELADDR;
    req.i.ifa_family = AF_INET;
    req.i.ifa_prefixlen = static_cast<unsigned char>(prefixLen);
    req.i.ifa_scope = RT_SCOPE_UNIVERSE;
    req.i.ifa_index = ifindex;

    struct in_addr in;
    if (inet_pton(AF_INET, addr.c_str(), &in) != 1) {
        return false;
    }

    struct rtattr* rta = reinterpret_cast<struct rtattr*>(reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.n.nlmsg_len));
    rta->rta_type = IFA_LOCAL;
    rta->rta_len = RTA_LENGTH(sizeof(in));
    std::memcpy(RTA_DATA(rta), &in, sizeof(in));
    req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    rta = reinterpret_cast<struct rtattr*>(reinterpret_cast<char*>(&req) + NLMSG_ALIGN(req.n.nlmsg_len));
    rta->rta_type = IFA_ADDRESS;
    rta->rta_len = RTA_LENGTH(sizeof(in));
    std::memcpy(RTA_DATA(rta), &in, sizeof(in));
    req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) + RTA_ALIGN(rta->rta_len);

    const int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0) {
        return false;
    }
    struct sockaddr_nl sa = {};
    sa.nl_family = AF_NETLINK;
    bool ok = false;
    if (sendto(fd, &req.n, req.n.nlmsg_len, 0, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) >= 0) {
        char buf[8192];
        const ssize_t len = recv(fd, buf, sizeof(buf), 0);
        if (len >= 0) {
            unsigned int remaining = static_cast<unsigned int>(len);
            for (struct nlmsghdr* h = reinterpret_cast<struct nlmsghdr*>(buf);
                 NLMSG_OK(h, remaining);
                 h = NLMSG_NEXT(h, remaining)) {
                if (h->nlmsg_type == NLMSG_ERROR) {
                    const struct nlmsgerr* err = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(h));
                    ok = (err->error == 0);
                    break;
                }
            }
        }
    }
    close(fd);
    return ok;
}

// Returns the ifindex of the first up, non-loopback IPv4 interface whose
// subnet contains `targetIp`, or -1 if none matches.
int interfaceIndexContaining(const std::string& targetIp) {
    struct in_addr t = {};
    if (inet_pton(AF_INET, targetIp.c_str(), &t) != 1) {
        return -1;
    }
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) {
        return -1;
    }
    int result = -1;
    for (struct ifaddrs* p = ifa; p != nullptr; p = p->ifa_next) {
        if (p->ifa_addr == nullptr || p->ifa_netmask == nullptr || p->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((p->ifa_flags & IFF_UP) == 0 || (p->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        const struct sockaddr_in* ip = reinterpret_cast<const struct sockaddr_in*>(p->ifa_addr);
        const struct sockaddr_in* mask = reinterpret_cast<const struct sockaddr_in*>(p->ifa_netmask);
        if ((ip->sin_addr.s_addr & mask->sin_addr.s_addr) == (t.s_addr & mask->sin_addr.s_addr)) {
            result = if_nametoindex(p->ifa_name);
            break;
        }
    }
    freeifaddrs(ifa);
    return result;
}

// True if any interface already has a 169.254.0.0/16 (link-local) address, in
// which case AutoIP cameras are already reachable and no bridge is needed.
bool hasLinkLocalAddress() {
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0) {
        return false;
    }
    bool found = false;
    for (struct ifaddrs* p = ifa; p != nullptr; p = p->ifa_next) {
        if (p->ifa_addr == nullptr || p->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        const struct sockaddr_in* ip = reinterpret_cast<const struct sockaddr_in*>(p->ifa_addr);
        const uint32_t host = ntohl(ip->sin_addr.s_addr);
        if ((host >> 16) == 0xA9FE) { // 169.254.x.x
            found = true;
            break;
        }
    }
    freeifaddrs(ifa);
    return found;
}

// Deterministic per-camera link-local address derived from the last two octets
// of the camera MAC, e.g. 0030531A568E -> 169.254.86.142. Keeps the temporary
// address unique per camera so multiple cameras on the same link never clash.
std::string linkLocalAddressForMac(const std::string& mac) {
    const auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    if (mac.size() < 4) {
        return "169.254.0.1";
    }
    const std::string tail = mac.substr(mac.size() - 4);
    const int b1 = hexVal(tail[0]) * 16 + hexVal(tail[1]);
    const int b2 = hexVal(tail[2]) * 16 + hexVal(tail[3]);
    return "169.254." + std::to_string(b1) + "." + std::to_string(b2);
}

// RAII-style holder for the temporary link-local address; removes it on
// destruction so every early-return path cleans up after the IP write.
struct LinkLocalBridgeGuard {
    int ifindex = -1;
    std::string addr;
    bool active = false;

    ~LinkLocalBridgeGuard() {
        if (active) {
            setSecondaryIpv4Address(ifindex, addr, 16, /*add=*/false);
            std::cout << "[CameraManager] Removed temporary link-local address "
                      << addr << " from interface index " << ifindex << std::endl;
            active = false;
        }
    }
};

int clampNodeValue(GenApi::CIntegerPtr node, int requested, const char* nodeName) {
    if (!node || !IsWritable(node)) {
        return requested;
    }

    const int minValue = static_cast<int>(node->GetMin());
    const int maxValue = static_cast<int>(node->GetMax());
    const int increment = std::max(1, static_cast<int>(node->GetInc()));

    int clamped = std::clamp(requested, minValue, maxValue);
    if (increment > 1) {
        clamped = minValue + ((clamped - minValue) / increment) * increment;
        clamped = std::clamp(clamped, minValue, maxValue);
    }

    if (clamped != requested) {
        std::cout << "[CameraManager] Adjusted " << nodeName << " from " << requested
                  << " to supported value " << clamped << std::endl;
    }

    node->SetValue(clamped);
    return clamped;
}

enum class GainNodeKind {
    None,
    Gain,
    GainAbs,
    GainRaw
};

const char* gainNodeDisplayName(GainNodeKind kind) {
    switch (kind) {
    case GainNodeKind::Gain:
        return "Gain";
    case GainNodeKind::GainAbs:
        return "Gain Abs";
    case GainNodeKind::GainRaw:
        return "Gain Raw";
    default:
        return "Gain";
    }
}

bool tryReadGainValue(GenApi::INodeMap& nodemap, double& gain, double* minGain = nullptr, double* maxGain = nullptr, GainNodeKind* kind = nullptr) {
    try {
        GenApi::CFloatPtr gainNode(nodemap.GetNode("Gain"));
        if (gainNode && IsReadable(gainNode)) {
            gain = gainNode->GetValue();
            if (minGain) *minGain = gainNode->GetMin();
            if (maxGain) *maxGain = gainNode->GetMax();
            if (kind) *kind = GainNodeKind::Gain;
            return true;
        }
    } catch (...) {}

    try {
        GenApi::CFloatPtr gainAbsNode(nodemap.GetNode("GainAbs"));
        if (gainAbsNode && IsReadable(gainAbsNode)) {
            gain = gainAbsNode->GetValue();
            if (minGain) *minGain = gainAbsNode->GetMin();
            if (maxGain) *maxGain = gainAbsNode->GetMax();
            if (kind) *kind = GainNodeKind::GainAbs;
            return true;
        }
    } catch (...) {}

    try {
        GenApi::CIntegerPtr gainRawNode(nodemap.GetNode("GainRaw"));
        if (gainRawNode && IsReadable(gainRawNode)) {
            gain = static_cast<double>(gainRawNode->GetValue());
            if (minGain) *minGain = static_cast<double>(gainRawNode->GetMin());
            if (maxGain) *maxGain = static_cast<double>(gainRawNode->GetMax());
            if (kind) *kind = GainNodeKind::GainRaw;
            return true;
        }
    } catch (...) {}

    return false;
}

bool tryWriteGainValue(GenApi::INodeMap& nodemap, double gain) {
    try {
        GenApi::CFloatPtr gainNode(nodemap.GetNode("Gain"));
        if (gainNode && IsWritable(gainNode)) {
            gainNode->SetValue(std::max(gainNode->GetMin(), std::min(gain, gainNode->GetMax())));
            return true;
        }
    } catch (...) {}

    try {
        GenApi::CFloatPtr gainAbsNode(nodemap.GetNode("GainAbs"));
        if (gainAbsNode && IsWritable(gainAbsNode)) {
            gainAbsNode->SetValue(std::max(gainAbsNode->GetMin(), std::min(gain, gainAbsNode->GetMax())));
            return true;
        }
    } catch (...) {}

    try {
        GenApi::CIntegerPtr gainRawNode(nodemap.GetNode("GainRaw"));
        if (gainRawNode && IsWritable(gainRawNode)) {
            const int requested = static_cast<int>(std::llround(gain));
            const int minValue = static_cast<int>(gainRawNode->GetMin());
            const int maxValue = static_cast<int>(gainRawNode->GetMax());
            const int increment = std::max(1, static_cast<int>(gainRawNode->GetInc()));
            int clamped = std::clamp(requested, minValue, maxValue);
            if (increment > 1) {
                clamped = minValue + ((clamped - minValue) / increment) * increment;
                clamped = std::clamp(clamped, minValue, maxValue);
            }
            gainRawNode->SetValue(clamped);
            return true;
        }
    } catch (...) {}

    return false;
}

// Reads the full set of scout/SFNC device nodes into LiveDeviceSettings.
// Safe against missing/read-protected nodes; each read is independently
// guarded. Also resolves chunk enablement via ChunkSelector/ChunkEnable.
void fillLiveSettingsFromNodeMap(GenApi::INodeMap& nodemap, CameraManager::LiveDeviceSettings& out) {
    const auto readEnum = [&](const char* name, QString& target) {
        try {
            GenApi::CEnumerationPtr e(nodemap.GetNode(name));
            if (e && GenApi::IsReadable(e)) {
                target = QString::fromLatin1(e->GetCurrentEntry()->GetSymbolic().c_str());
            }
        } catch (...) {}
    };
    const auto readInt = [&](const char* name, int& target) {
        try {
            GenApi::CIntegerPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsReadable(n)) {
                target = static_cast<int>(n->GetValue());
            }
        } catch (...) {}
    };
    const auto readFloat = [&](const char* name, double& target) {
        try {
            GenApi::CFloatPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsReadable(n)) {
                target = n->GetValue();
            }
        } catch (...) {}
    };
    const auto readBool = [&](const char* name, bool& target) {
        try {
            GenApi::CBooleanPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsReadable(n)) {
                target = n->GetValue();
            }
        } catch (...) {}
    };
    const auto readString = [&](const char* name, QString& target) {
        try {
            GenApi::CStringPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsReadable(n)) {
                target = QString::fromLatin1(n->GetValue().c_str());
            }
        } catch (...) {}
    };

    readEnum("PixelFormat", out.pixelFormat);
    readInt("Width", out.width);
    readInt("Height", out.height);
    readInt("OffsetX", out.offsetX);
    readInt("OffsetY", out.offsetY);
    readFloat("ExposureTimeAbs", out.exposureUs);
    if (out.exposureUs <= 0.0) {
        readFloat("ExposureTime", out.exposureUs); // newer SFNC fallback
    }
    readFloat("ExposureTimeBaseAbs", out.exposureTimeBaseAbs);
    readInt("ExposureTimeRaw", out.exposureTimeRaw);
    readBool("AcquisitionFrameRateEnable", out.acquisitionFrameRateEnable);
    readFloat("AcquisitionFrameRateAbs", out.acquisitionFrameRate);
    if (out.acquisitionFrameRate <= 0.0) {
        readFloat("AcquisitionFrameRate", out.acquisitionFrameRate); // SFNC fallback
    }
    readFloat("ResultingFrameRateAbs", out.resultingFrameRate);
    if (out.resultingFrameRate <= 0.0) {
        readFloat("ResultingFrameRate", out.resultingFrameRate);
    }
    readBool("ChunkModeActive", out.chunkModeActive);

    // Chunk enablement: iterate the dialog's chunk option names, select each
    // via ChunkSelector and read ChunkEnable.
    if (GenApi::INode* selector = nodemap.GetNode("ChunkSelector")) {
        GenApi::CEnumerationPtr sel(selector);
        GenApi::CBooleanPtr enable(nodemap.GetNode("ChunkEnable"));
        if (sel && enable && GenApi::IsReadable(sel) && GenApi::IsReadable(enable)) {
            const QStringList candidates = {
                "Image", "OffsetX", "OffsetY", "Width", "Height", "PixelFormat",
                "DynamicRangeMax", "DynamicRangeMin", "Timestamp", "Framecounter"
            };
            for (const QString& name : candidates) {
                try {
                    sel->FromString(name.toLatin1().constData());
                    if (enable->GetValue()) {
                        out.enabledChunks << name;
                    }
                } catch (...) {
                    // selector not supported on this camera — skip
                }
            }
        }
    }

    // Temperature: scout exposes TemperatureSelector + TemperatureAbs.
    try {
        GenApi::CEnumerationPtr tempSel(nodemap.GetNode("TemperatureSelector"));
        GenApi::CFloatPtr tempAbs(nodemap.GetNode("TemperatureAbs"));
        if (tempAbs && GenApi::IsReadable(tempAbs)) {
            if (tempSel && GenApi::IsReadable(tempSel)) {
                // Prefer the sensor board reading, fall back to the first entry.
                try {
                    tempSel->FromString("Sensorboard");
                } catch (...) {
                    try {
                        tempSel->FromString("Mainboard");
                    } catch (...) {}
                }
            }
            out.temperature = tempAbs->GetValue();
        }
    } catch (...) {}

    readString("DeviceVendorName", out.vendorName);
    readString("DeviceManufacturerInfo", out.manufacturerInfo);
    readString("DeviceVersion", out.deviceVersion);
    readString("DeviceFirmwareVersion", out.firmwareVersion);
    readString("DeviceID", out.deviceId);
}

// Writes the live (non-stop-required) exposure/rate nodes. Mirrors the
// startup configureCamera() writes: base/raw are applied when enabled,
// absolute exposure always, frame rate per its enable flag. These nodes
// accept changes while the camera is grabbing (Basler scout behavior —
// no acquisition stop required).
void writeExposureRateNodes(GenApi::INodeMap& nodemap, const CameraInfo& info) {
    const auto setBool = [&](const char* name, bool value) {
        try {
            GenApi::CBooleanPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsWritable(n)) {
                n->SetValue(value);
            }
        } catch (...) {}
    };
    const auto setFloat = [&](const char* name, double value) {
        try {
            GenApi::CFloatPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsWritable(n)) {
                n->SetValue(value);
            }
        } catch (...) {}
    };
    const auto setInt = [&](const char* name, int value) {
        try {
            GenApi::CIntegerPtr n(nodemap.GetNode(name));
            if (n && GenApi::IsWritable(n)) {
                n->SetValue(value);
            }
        } catch (...) {}
    };

    setBool("ExposureTimeBaseEnable", info.enableExposureTimeBase);
    setBool("EnableExposureTimeBase", info.enableExposureTimeBase);
    setFloat("ExposureTimeAbs", info.exposureTimeAbs);
    setFloat("ExposureTime", info.exposureTimeAbs); // newer SFNC fallback
    if (info.enableExposureTimeBase) {
        setFloat("ExposureTimeBaseAbs", info.exposureTimeBaseAbs);
        setInt("ExposureTimeRaw", info.exposureTimeRaw);
    }

    setBool("AcquisitionFrameRateEnable", info.enableAcquisitionFps);
    setBool("AcquisitionFrameRateEnabled", info.enableAcquisitionFps);
    if (info.enableAcquisitionFps) {
        setFloat("AcquisitionFrameRateAbs", info.fps);
        setFloat("AcquisitionFrameRate", info.fps); // SFNC fallback
    }
}

// Opens the configured camera (by MAC/IP from CameraConfig) directly and
// invokes fn with its node map + device info. Returns false when the device
// is absent or the open fails (e.g. it is owned by the acquisition runtime).
bool openConfiguredDeviceDirect(int configArrayIndex,
                                const std::function<void(GenApi::INodeMap&, const Pylon::CDeviceInfo&)>& fn) {
    const std::vector<CameraInfo> cameras = CameraConfig::getCameras();
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameras.size())) {
        return false;
    }
    const QString wantedMac = cameras[configArrayIndex].macAddress.trimmed();
    const QString wantedIp = cameras[configArrayIndex].ipAddress.trimmed();
    if (wantedMac.isEmpty() && wantedIp.isEmpty()) {
        return false;
    }

    try {
        Pylon::CTlFactory& TlFactory = Pylon::CTlFactory::GetInstance();
        Pylon::IGigETransportLayer* pTl = dynamic_cast<Pylon::IGigETransportLayer*>(TlFactory.CreateTl(Pylon::BaslerGigEDeviceClass));
        if (pTl == nullptr) {
            return false;
        }

        Pylon::DeviceInfoList_t lstDevices;
        pTl->EnumerateAllDevices(lstDevices);
        const std::string targetMac = normalizeMacAddress(wantedMac.toStdString());

        Pylon::CDeviceInfo matchedDeviceInfo;
        bool found = false;
        for (const auto& dev : lstDevices) {
            const std::string enumeratedMac = normalizeMacAddress(dev.GetMacAddress().c_str());
            if ((!targetMac.empty() && enumeratedMac == targetMac)
                    || (!wantedIp.isEmpty() && QString::fromLatin1(dev.GetIpAddress().c_str()) == wantedIp)) {
                matchedDeviceInfo = dev;
                found = true;
                break;
            }
        }

        if (!found) {
            TlFactory.ReleaseTl(pTl);
            return false;
        }

        try {
            std::unique_ptr<Pylon::IPylonDevice> device(TlFactory.CreateDevice(matchedDeviceInfo));
            Pylon::CBaslerGigEInstantCamera camera(device.release());
            camera.Open();
            fn(camera.GetNodeMap(), camera.GetDeviceInfo());
            camera.Close();
            TlFactory.ReleaseTl(pTl);
            return true;
        } catch (const Pylon::GenericException& e) {
            std::cerr << "[CameraManager] Direct device open failed: " << e.GetDescription() << std::endl;
            TlFactory.ReleaseTl(pTl);
            return false;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Direct device access error: " << e.GetDescription() << std::endl;
    }
    return false;
}

void loadCameraDefaultUserSet(Pylon::CInstantCamera& camera) {
    try {
        if (!camera.IsPylonDeviceAttached() || !camera.IsOpen()) {
            return;
        }

        GenApi::INodeMap& nodemap = camera.GetNodeMap();

        try {
            GenApi::CEnumerationPtr userSetDefault(nodemap.GetNode("UserSetDefault"));
            if (userSetDefault && IsWritable(userSetDefault)
                && GenApi::IsAvailable(userSetDefault->GetEntryByName("Default"))) {
                userSetDefault->FromString("Default");
                std::cout << "[CameraManager] UserSetDefault -> Default" << std::endl;
            }
        } catch (...) {}

        try {
            GenApi::CEnumerationPtr userSetDefaultSelector(nodemap.GetNode("UserSetDefaultSelector"));
            if (userSetDefaultSelector && IsWritable(userSetDefaultSelector)
                && GenApi::IsAvailable(userSetDefaultSelector->GetEntryByName("Default"))) {
                userSetDefaultSelector->FromString("Default");
                std::cout << "[CameraManager] UserSetDefaultSelector -> Default" << std::endl;
            }
        } catch (...) {}

        GenApi::CEnumerationPtr userSetSelector(nodemap.GetNode("UserSetSelector"));
        GenApi::CCommandPtr userSetLoad(nodemap.GetNode("UserSetLoad"));
        if (userSetSelector && IsWritable(userSetSelector)
            && userSetLoad && IsWritable(userSetLoad)
            && GenApi::IsAvailable(userSetSelector->GetEntryByName("Default"))) {
            userSetSelector->FromString("Default");
            userSetLoad->Execute();
            std::cout << "[CameraManager] Loaded UserSet Default into active set" << std::endl;

            double gainValue = 0.0;
            double exposureValue = 0.0;
            GainNodeKind gainKind = GainNodeKind::None;
            if (tryReadGainValue(nodemap, gainValue, nullptr, nullptr, &gainKind)) {
                std::cout << "[CameraManager] Startup user set " << gainNodeDisplayName(gainKind)
                          << "=" << gainValue << std::endl;
            }
            try {
                GenApi::CFloatPtr exposureNode(nodemap.GetNode("ExposureTimeAbs"));
                if ((!exposureNode || !IsReadable(exposureNode)) && nodemap.GetNode("ExposureTime")) {
                    exposureNode = GenApi::CFloatPtr(nodemap.GetNode("ExposureTime"));
                }
                if (exposureNode && IsReadable(exposureNode)) {
                    exposureValue = exposureNode->GetValue();
                    std::cout << "[CameraManager] Startup user set exposure=" << exposureValue << std::endl;
                }
            } catch (...) {}
        }
    } catch (const Pylon::GenericException& e) {
        std::cout << "[CameraManager] User set default load skipped: " << e.GetDescription() << std::endl;
    }
}
}

// DeviceRemovalHandler Implementation
void CameraManager::DeviceRemovalHandler::OnCameraDeviceRemoved(Pylon::CInstantCamera& camera) {
    try {
        if (!manager_) return;

        // During intentional shutdown/restart, camera teardown can trigger removal
        // callbacks. Ignore them to avoid racing with stopAcquisition() cleanup.
        if (manager_->shuttingDown_) {
            return;
        }

        std::cout << "[CameraManager] DEVICE REMOVAL EVENT: camera context="
                  << camera.GetCameraContext() << std::endl;

        // Camera context stores config array index in the per-camera runtime model.
        uint32_t configIdx = static_cast<uint32_t>(camera.GetCameraContext());

        // 1. Mark this camera as disconnected so the acquisition loop skips it gracefully.
        //    Do NOT touch acquiring_ — surviving cameras must keep streaming uninterrupted.
        {
            std::lock_guard<std::mutex> lock(manager_->disconnectedMutex_);
            manager_->disconnectedCameras_.insert(configIdx);
        }

        // 2. Blank ONLY the disconnected camera's UI tile.
        {
            std::lock_guard<std::mutex> lock(manager_->callbackMutex_);
            if (manager_->callback_) {
                if (configIdx < manager_->cameraRuntimes_.size()) {
                    manager_->callback_(static_cast<int>(configIdx), cv::Mat());
                }
            }
        }

        // 3. Launch the background recovery thread to wait for the camera to reappear and
        //    rebuild only the missing slot.  If a recovery is already running, it will
        //    handle the newly-disconnected camera on its next poll cycle.
        if (!manager_->shuttingDown_) {
            manager_->startRecoveryThreadIfNeeded();
        }
    } catch (const std::exception& e) {
        std::cerr << "[CameraManager] Exception in DeviceRemovalHandler: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[CameraManager] Unknown exception in DeviceRemovalHandler" << std::endl;
    }
}

CameraManager::CameraManager(int numCameras) 
    : numCameras_(numCameras), acquiring_(false), recovering_(false), width_(780), height_(580), fps_(10.0), 
      defectDetectionEnabled_(false) {
    prevTempStatus_.assign(numCameras, TemperatureStatus::Unknown);
    // Pylon requires initialization
    
    // Check environment for emulation mode (set externally via docker-compose or workflow script)
    const char* pylonCamEmu = getenv("PYLON_CAMEMU");
    if (pylonCamEmu) {
        std::cout << "[CameraManager] PYLON_CAMEMU detected in environment (value=" << pylonCamEmu 
                  << "). Running in EMULATION mode." << std::endl;
    } else {
        std::cout << "[CameraManager] No PYLON_CAMEMU in environment. Searching for REAL cameras." << std::endl;
    }

    try {
        PylonInitialize();
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to initialize Pylon: " << e.GetDescription() << std::endl;
    }

    // Use centralized camera labels from config
    for (int i = 0; i < numCameras_; ++i) {
        cameraLabels_.push_back(CameraConfig::getCameraLabel(i).toStdString());
        modelNames_.push_back("Unknown Model"); // Default
        snapshotRequests_.push_back(false);
        swGain_.push_back(1.0);
        swGamma_.push_back(1.0);
        swContrast_.push_back(1.0);
        lutCache_.push_back(cv::Mat());
        lutValid_.push_back(false);
        softwareFrameCounters_.emplace_back(0);
    }
    cameraRuntimes_.resize(numCameras_);
}

CameraManager::~CameraManager() {
    stopAcquisition();
    try {
        PylonTerminate();
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to terminate Pylon: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::startRecoveryThreadIfNeeded() {
    if (shuttingDown_ || recovering_) {
        return;
    }

    std::lock_guard<std::mutex> lock(recoveryThreadMutex_);
    if (shuttingDown_ || recovering_) {
        return;
    }

    // Mark as running before thread launch to avoid races where multiple callers
    // observe recovering_ == false and spawn/join competing recovery threads.
    recovering_ = true;

    if (recoveryThread_.joinable()) {
        recoveryThread_.join();
    }
    recoveryThread_ = std::thread(&CameraManager::recoveryLoop, this);
}

void CameraManager::joinRecoveryThread() {
    std::lock_guard<std::mutex> lock(recoveryThreadMutex_);
    if (recoveryThread_.joinable()) {
        recoveryThread_.join();
    }
}

void CameraManager::requestCameraReconnect(int configArrayIndex) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(disconnectedMutex_);
        disconnectedCameras_.insert(static_cast<uint32_t>(configArrayIndex));
    }
    // If a recovery thread is already running it will pick up the new index on
    // its next poll; otherwise start one now.
    startRecoveryThreadIfNeeded();
}

bool CameraManager::attachConfiguredCamera(int configArrayIndex, const CameraInfo& camInfo,
                                           const Pylon::DeviceInfoList_t& devices,
                                           std::set<int>& claimedDeviceIndices,
                                           bool suppressBlank) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }

    CameraRuntime& runtime = cameraRuntimes_[configArrayIndex];
    runtime.configId = camInfo.id;
    runtime.source = camInfo.source;
    runtime.connected = false;
    runtime.targetDevice = Pylon::CDeviceInfo();
    runtime.camera.reset();

    cameraIndexToConfigId_[configArrayIndex] = camInfo.id;
    configArrayIndexToPylonIndex_[configArrayIndex] = -1;
    pylonIndexToConfigArrayIndex_[configArrayIndex] = configArrayIndex;

    if (camInfo.source == 2) {
        if (!suppressBlank) {
            clearCameraTile(configArrayIndex);
        }
        return false;
    }

    int matchedDeviceIndex = -1;
    for (int devIndex = 0; devIndex < static_cast<int>(devices.size()); ++devIndex) {
        if (claimedDeviceIndices.count(devIndex)) {
            continue;
        }

        const auto& dev = devices[devIndex];
        const bool isEmulatedDevice = (dev.GetDeviceClass() == "BaslerCamEmu");
        bool canMatch = false;

        if (camInfo.source == 0 && isEmulatedDevice) {
            canMatch = true;
        } else if (camInfo.source == 1 && !isEmulatedDevice && !camInfo.macAddress.isEmpty() &&
                   camInfo.macAddress != "None / Auto" &&
                   normalizeMacAddress(camInfo.macAddress.toStdString()) == normalizeMacAddress(dev.GetMacAddress().c_str())) {
            canMatch = true;
        }

        if (canMatch) {
            matchedDeviceIndex = devIndex;
            break;
        }
    }

    if (matchedDeviceIndex < 0) {
        std::cerr << "[CameraManager] WARNING: Could not find matching physical device for Camera ID "
                  << camInfo.id << " (Source: " << (camInfo.source == 0 ? "Emulated" : "Real") << ")" << std::endl;
        if (!suppressBlank) {
            clearCameraTile(configArrayIndex);
        }
        return false;
    }

    claimedDeviceIndices.insert(matchedDeviceIndex);
    const auto& matchedDevice = devices[matchedDeviceIndex];

    try {
        CTlFactory& tlFactory = CTlFactory::GetInstance();
        runtime.camera = std::make_unique<CInstantCamera>(tlFactory.CreateDevice(matchedDevice));
        runtime.camera->RegisterConfiguration(new DeviceRemovalHandler(this), RegistrationMode_Append, Cleanup_Delete);
        runtime.camera->SetCameraContext(configArrayIndex);
        runtime.targetDevice = matchedDevice;
        runtime.connected = true;

        configArrayIndexToPylonIndex_[configArrayIndex] = configArrayIndex;
        pylonIndexToConfigArrayIndex_[configArrayIndex] = configArrayIndex;

        std::cout << "[CameraManager] Config array index " << configArrayIndex
                  << " (Config ID " << camInfo.id << ") attached to device "
                  << matchedDevice.GetModelName() << std::endl;

        if (configArrayIndex < static_cast<int>(modelNames_.size())) {
            modelNames_[configArrayIndex] = matchedDevice.GetModelName().c_str();
        }

        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to attach camera for config index " << configArrayIndex
                  << ": " << e.GetDescription() << std::endl;
        runtime.camera.reset();
        runtime.connected = false;
        configArrayIndexToPylonIndex_[configArrayIndex] = -1;
        if (!suppressBlank) {
            clearCameraTile(configArrayIndex);
        }
        return false;
    }
}

void CameraManager::clearCameraTile(int configArrayIndex) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (callback_ && configArrayIndex >= 0) {
        callback_(configArrayIndex, cv::Mat());
    }
}

Pylon::CInstantCamera* CameraManager::getCameraByConfigIndex(int configArrayIndex) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return nullptr;
    }
    return cameraRuntimes_[configArrayIndex].camera.get();
}

const Pylon::CInstantCamera* CameraManager::getCameraByConfigIndex(int configArrayIndex) const {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return nullptr;
    }
    return cameraRuntimes_[configArrayIndex].camera.get();
}

bool CameraManager::isCameraConnected(int configArrayIndex) const {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }
    return cameraRuntimes_[configArrayIndex].connected && cameraRuntimes_[configArrayIndex].camera != nullptr;
}

bool CameraManager::isCameraOpen(int configArrayIndex) const {
    const auto* camera = getCameraByConfigIndex(configArrayIndex);
    return camera && camera->IsPylonDeviceAttached() && camera->IsOpen();
}

uint64_t CameraManager::getIncompleteGrabCount(int configArrayIndex) const {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return 0;
    }
    return cameraRuntimes_[configArrayIndex].incompleteGrabCount;
}

uint64_t CameraManager::getConsecutiveIncompleteGrabCount(int configArrayIndex) const {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return 0;
    }
    return cameraRuntimes_[configArrayIndex].consecutiveIncompleteGrabCount;
}

bool CameraManager::isCameraRunning(int configArrayIndex) const {
    const auto* camera = getCameraByConfigIndex(configArrayIndex);
    return cameraRuntimes_.size() > static_cast<size_t>(configArrayIndex)
        && configArrayIndex >= 0
        && cameraRuntimes_[configArrayIndex].connected
        && camera
        && camera->IsPylonDeviceAttached()
        && camera->IsOpen()
        && camera->IsGrabbing();
}

bool CameraManager::stopCamera(int configArrayIndex) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }

    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return false;
    }

    try {
        if (camera->IsGrabbing()) {
            camera->StopGrabbing();
        }
        if (cameraRuntimes_[configArrayIndex].grabThread.joinable()) {
            cameraRuntimes_[configArrayIndex].grabThread.join();
        }
        cameraRuntimes_[configArrayIndex].connected = true;
        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to stop camera " << configArrayIndex << ": "
                  << e.GetDescription() << std::endl;
        return false;
    }
}

bool CameraManager::startCamera(int configArrayIndex, const CameraInfo& config) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }

    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return false;
    }

    try {
        if (!camera->IsOpen()) {
            camera->Open();
        }
        if (camera->GetDeviceInfo().GetDeviceClass() != "BaslerCamEmu") {
            loadCameraDefaultUserSet(*camera);
        }
        configureCamera(camera->GetNodeMap(), config, camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu", true);
        if (!camera->IsGrabbing()) {
            camera->StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByUser);
        }
        cameraRuntimes_[configArrayIndex].connected = true;
        if (!cameraRuntimes_[configArrayIndex].grabThread.joinable()) {
            cameraRuntimes_[configArrayIndex].grabThread = std::thread(&CameraManager::acquisitionLoop, this, configArrayIndex);
        }
        setCameraFrameRate(configArrayIndex, config.fps, config.enableAcquisitionFps);
        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to start camera " << configArrayIndex << ": "
                  << e.GetDescription() << std::endl;
        return false;
    }
}

bool CameraManager::applyCameraDeviceSettings(int configArrayIndex, const CameraInfo& config) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return false;
    }

    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !camera->IsPylonDeviceAttached()) {
        return false;
    }

    try {
        if (!camera->IsOpen()) {
            camera->Open();
        }
        if (camera->GetDeviceInfo().GetDeviceClass() != "BaslerCamEmu") {
            loadCameraDefaultUserSet(*camera);
        }
        configureCamera(camera->GetNodeMap(), config, camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu", false);
        setCameraFrameRate(configArrayIndex, config.fps, config.enableAcquisitionFps);
        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Failed to apply device settings for camera " << configArrayIndex << ": "
                  << e.GetDescription() << std::endl;
        return false;
    }
}

void CameraManager::stopCameraRuntime(int configArrayIndex) {
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        return;
    }

    CameraRuntime& runtime = cameraRuntimes_[configArrayIndex];

    // Stop the stream first so the grab loop can exit, but do not destroy the
    // camera object until the worker thread has fully joined.
    try {
        if (runtime.camera && runtime.camera->IsGrabbing()) {
            runtime.camera->StopGrabbing();
        }
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] stopCameraRuntime stop warning: " << e.GetDescription() << std::endl;
    }

    if (runtime.grabThread.joinable()) {
        runtime.grabThread.join();
    }

    try {
        if (runtime.camera && runtime.camera->IsOpen()) {
            runtime.camera->Close();
        }
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] stopCameraRuntime warning: " << e.GetDescription() << std::endl;
    }

    runtime.connected = false;
    runtime.camera.reset();
    // Guard: configArrayIndexToPylonIndex_ may be empty if initialize() was never
    // called (e.g. stopAcquisition() is called before a successful initialize()).
    if (configArrayIndex < static_cast<int>(configArrayIndexToPylonIndex_.size())) {
        configArrayIndexToPylonIndex_[configArrayIndex] = -1;
    }
}

bool CameraManager::initialize(const std::set<int>& suppressBlankFor) {
    try {
        CTlFactory& tlFactory = CTlFactory::GetInstance();
        DeviceInfoList_t devices;
        
        // Find all attached devices. 
        if (tlFactory.EnumerateDevices(devices) == 0) {
            std::cerr << "[CameraManager] No cameras found!" << std::endl;
            return false;
        }

        std::cout << "[CameraManager] Found " << devices.size() << " Pylon devices." << std::endl;
        for (const auto& dev : devices) {
            std::cout << "  - DeviceClass: " << dev.GetDeviceClass() << " | MAC: " << (dev.GetDeviceClass() != "BaslerCamEmu" ? dev.GetMacAddress().c_str() : "N/A") << std::endl;
        }

        // Get configured cameras from CameraConfig
        std::vector<CameraInfo> configuredCams = CameraConfig::getCameras();
        numCameras_ = static_cast<int>(configuredCams.size());
        
        std::cout << "[CameraManager] Loaded " << configuredCams.size() << " Camera configs." << std::endl;
        for (const auto& c : configuredCams) {
            std::cout << "[CameraManager] Config - ID: " << c.id << " Source: " << c.source << " MAC: " << c.macAddress.toStdString() << std::endl;
        }
        
        // Resize all per-camera vectors to match the new camera count.
        // This is critical: initialize() can be called after config changes (e.g.
        // assigning a MAC in System Configuration). Without resizing here, the
        // acquisition loop accesses swGain_/swGamma_/etc. out-of-bounds → crash.
        const size_t newCount = configuredCams.size();
        cameraRuntimes_.resize(newCount);

        auto resizeAndFill = [&](auto& vec, auto defaultVal) {
            vec.resize(newCount, defaultVal);
        };
        resizeAndFill(cameraLabels_,    std::string("Cam"));
        resizeAndFill(modelNames_,      std::string("Unknown Model"));
        resizeAndFill(snapshotRequests_, false);
        resizeAndFill(swGain_,          1.0);
        resizeAndFill(swGamma_,         1.0);
        resizeAndFill(swContrast_,      1.0);
        resizeAndFill(lutCache_,        cv::Mat());
        resizeAndFill(lutValid_,        false);
        prevTempStatus_.resize(newCount, TemperatureStatus::Unknown);

        // softwareFrameCounters_ is a plain int64_t vector (one slot per camera,
        // each slot written only by its own acquisition thread). Resize like others.
        softwareFrameCounters_.resize(newCount, 0);

        // Refresh camera labels from config
        for (size_t i = 0; i < newCount; ++i) {
            cameraLabels_[i] = CameraConfig::getCameraLabel(static_cast<int>(i)).toStdString();
        }

        cameraIndexToConfigId_.assign(newCount, -1);
        configArrayIndexToPylonIndex_.assign(newCount, -1);
        pylonIndexToConfigArrayIndex_.assign(newCount, -1);

        std::set<int> claimedDeviceIndices;
        for (int cfgArrayIdx = 0; cfgArrayIdx < static_cast<int>(configuredCams.size()); ++cfgArrayIdx) {
            const bool suppressBlank = suppressBlankFor.find(cfgArrayIdx) != suppressBlankFor.end();
            attachConfiguredCamera(cfgArrayIdx, configuredCams[cfgArrayIdx], devices, claimedDeviceIndices, suppressBlank);
        }

        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during initialization: " 
                  << e.GetDescription() << std::endl;
        return false;
    }
}

// ============================================================
// Live Camera Parameter Adjustment (no acquisition restart needed)
// ============================================================

void CameraManager::setCameraGain(int cameraIndex, double gain) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraGain: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    // Gain in this UI is a hardware camera parameter, not a software preview multiplier.
    // In particular, older Basler cameras can expose GainRaw values like 320..1023.
    // Feeding those raw values into swGain_ would behave like a 320x software gain LUT
    // and blow the image out to white. Keep software preview gain neutral here.
    if (cameraIndex < (int)swGain_.size()) swGain_[cameraIndex] = 1.0;
    if (cameraIndex < (int)lutValid_.size()) lutValid_[cameraIndex] = false;
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            if (tryWriteGainValue(nodemap, gain)) {
                std::cout << "[CameraManager] setCameraGain: cam=" << cameraIndex << " gain=" << gain << " OK" << std::endl;
            } else {
                std::cerr << "[CameraManager] setCameraGain: cam " << cameraIndex
                          << " no writable Gain/GainAbs/GainRaw node" << std::endl;
            }
        } else {
            std::cerr << "[CameraManager] setCameraGain: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraGain: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraExposure(int cameraIndex, double exposureUs) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraExposure: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            GenApi::CFloatPtr exposureNode(nodemap.GetNode("ExposureTimeAbs")); // Basler scout / older SFNC
            if (!exposureNode || !GenApi::IsWritable(exposureNode)) {
                exposureNode = GenApi::CFloatPtr(nodemap.GetNode("ExposureTime")); // newer SFNC
            }

            if (exposureNode && GenApi::IsWritable(exposureNode)) {
                const double clamped = std::max(exposureNode->GetMin(), std::min(exposureUs, exposureNode->GetMax()));
                exposureNode->SetValue(clamped);
                std::cout << "[CameraManager] setCameraExposure: cam=" << cameraIndex
                          << " exp=" << clamped << " OK" << std::endl;
            } else {
                std::cerr << "[CameraManager] setCameraExposure: cam " << cameraIndex
                          << " no writable ExposureTimeAbs/ExposureTime node" << std::endl;
            }
        } else {
            std::cerr << "[CameraManager] setCameraExposure: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraExposure: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraGamma(int cameraIndex, double gamma) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraGamma: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    if (cameraIndex < (int)swGamma_.size()) swGamma_[cameraIndex] = std::max(0.01, gamma);
    if (cameraIndex < (int)lutValid_.size()) lutValid_[cameraIndex] = false;
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            Pylon::CFloatParameter(nodemap, "Gamma").SetValue(gamma);
            std::cout << "[CameraManager] setCameraGamma: cam=" << cameraIndex << " gamma=" << gamma << " OK" << std::endl;
        } else {
            std::cerr << "[CameraManager] setCameraGamma: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraGamma: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

CameraManager::CameraParams CameraManager::getCameraParams(int configArrayIndex) {
    CameraParams p;
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !(camera->IsPylonDeviceAttached() && camera->IsOpen()))
        return p;
    try {
        GenApi::INodeMap& nm = camera->GetNodeMap();

        // Gain
        GainNodeKind gainKind = GainNodeKind::None;
        if (tryReadGainValue(nm, p.gain, &p.gainMin, &p.gainMax, &gainKind)) {
            p.gainIsRaw = (gainKind == GainNodeKind::GainRaw);
            p.gainDisplayName = QString::fromLatin1(gainNodeDisplayName(gainKind));
        }

        // Exposure / Shutter
        GenApi::CFloatPtr e(nm.GetNode("ExposureTimeAbs"));
        if ((!e || !IsReadable(e)) && nm.GetNode("ExposureTime")) e = GenApi::CFloatPtr(nm.GetNode("ExposureTime"));
        if (e && IsReadable(e)) {
            p.exposureUs = e->GetValue();
            try { p.exposureMinUs = e->GetMin(); } catch (...) {}
            try { p.exposureMaxUs = e->GetMax(); } catch (...) {}
        }

        // Gamma
        GenApi::CFloatPtr gm(nm.GetNode("Gamma"));
        if (gm && IsReadable(gm)) p.gamma = gm->GetValue();

        // Contrast (Basler-specific)
        GenApi::CFloatPtr ct(nm.GetNode("BslContrast"));
        if (ct && IsReadable(ct)) p.contrast = ct->GetValue();

        // Resulting FPS
        GenApi::CFloatPtr fps(nm.GetNode("ResultingFrameRateAbs"));
        if (!fps || !IsReadable(fps)) fps = GenApi::CFloatPtr(nm.GetNode("ResultingFrameRate"));
        if (fps && IsReadable(fps)) p.fps = fps->GetValue();

        // WDR — BslDualGain nodes (optional, camera-family dependent)
        try {
            GenApi::CFloatPtr wdrH(nm.GetNode("BslDualGainHigh"));
            if (wdrH && IsReadable(wdrH)) p.wdrHigh = wdrH->GetValue();
        } catch (...) {}
        try {
            GenApi::CFloatPtr wdrL(nm.GetNode("BslDualGainLow"));
            if (wdrL && IsReadable(wdrL)) p.wdrLow = wdrL->GetValue();
        } catch (...) {}

        // Live output queue depth (frames queued in Pylon's internal buffer ring)
        try {
            if (camera->OutputQueueSize.IsReadable())
                p.outputQueueDepth = static_cast<int>(camera->OutputQueueSize.GetValue());
        } catch (...) {}

        // Resolution (for MB calculation: outputQueueDepth × W × H × bpp)
        try {
            GenApi::CIntegerPtr w(nm.GetNode("Width"));
            GenApi::CIntegerPtr h(nm.GetNode("Height"));
            if (w && IsReadable(w)) p.width  = static_cast<int>(w->GetValue());
            if (h && IsReadable(h)) p.height = static_cast<int>(h->GetValue());
        } catch (...) {}

        // Bytes per pixel — derive from PixelFormat node if available
        try {
            GenApi::CEnumerationPtr pf(nm.GetNode("PixelFormat"));
            if (pf && IsReadable(pf)) {
                std::string fmt = pf->GetCurrentEntry()->GetSymbolic().c_str();
                // Mono8, BayerXX8 → 1 byte; Mono12/16, BayerXX12/16 → 2 bytes; RGB8 → 3 bytes
                if (fmt.find("12") != std::string::npos || fmt.find("16") != std::string::npos)
                    p.bpp = 2;
                else if (fmt.find("RGB") != std::string::npos || fmt.find("BGR") != std::string::npos)
                    p.bpp = 3;
                else
                    p.bpp = 1;
            }
        } catch (...) {}

    } catch (...) {}
    return p;
}

CameraManager::LiveDeviceSettings CameraManager::readLiveDeviceSettings(int configArrayIndex, bool allowDirectOpen) {
    LiveDeviceSettings out;

    // 1) Runtime path: camera attached to the acquisition runtime.
    if (Pylon::CInstantCamera* camera = getCameraByConfigIndex(configArrayIndex)) {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            try {
                fillLiveSettingsFromNodeMap(camera->GetNodeMap(), out);
                const Pylon::CDeviceInfo& di = camera->GetDeviceInfo();
                out.modelName = QString::fromLatin1(di.GetModelName().c_str());
                out.ipAddress = QString::fromLatin1(di.GetIpAddress().c_str());
                out.ok = true;
                out.fromRuntime = true;
                return out;
            } catch (const Pylon::GenericException& e) {
                std::cerr << "[CameraManager] readLiveDeviceSettings runtime read failed for index "
                          << configArrayIndex << ": " << e.GetDescription() << std::endl;
            }
        }
    }

    // 2) Direct path: camera on the network but not attached to the runtime.
    //    This performs a blocking GigE open/close — callers that need a fast,
    //    non-blocking read (e.g. the Device Settings dialog constructor) can
    //    opt out and rely on the runtime path only.
    if (!allowDirectOpen) {
        return out;
    }
    if (openConfiguredDeviceDirect(configArrayIndex,
            [&out](GenApi::INodeMap& nodemap, const Pylon::CDeviceInfo& di) {
                fillLiveSettingsFromNodeMap(nodemap, out);
                out.modelName = QString::fromLatin1(di.GetModelName().c_str());
                out.ipAddress = QString::fromLatin1(di.GetIpAddress().c_str());
                if (out.modelName.isEmpty()) {
                    out.modelName = QString::fromLatin1(di.GetFriendlyName().c_str());
                }
            })) {
        out.ok = true;
        out.fromRuntime = false;
        std::cout << "[CameraManager] readLiveDeviceSettings direct read OK for index "
                  << configArrayIndex << " (" << out.modelName.toStdString()
                  << " @ " << out.ipAddress.toStdString() << ")" << std::endl;
    } else {
        std::cerr << "[CameraManager] readLiveDeviceSettings: no device matched for index "
                  << configArrayIndex << std::endl;
    }
    return out;
}

void CameraManager::applyLiveExposureRate(int configArrayIndex, const CameraInfo& info) {
    // Runtime path: camera attached to the acquisition runtime.
    if (Pylon::CInstantCamera* camera = getCameraByConfigIndex(configArrayIndex)) {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            try {
                writeExposureRateNodes(camera->GetNodeMap(), info);
                return;
            } catch (const Pylon::GenericException& e) {
                std::cerr << "[CameraManager] applyLiveExposureRate runtime write failed for index "
                          << configArrayIndex << ": " << e.GetDescription() << std::endl;
            }
        }
    }

    // Direct path: camera on the network but not attached. Exposure/rate
    // nodes accept writes without an acquisition stop, so a direct open is
    // safe even while the camera would be streaming for another owner.
    if (!openConfiguredDeviceDirect(configArrayIndex,
            [&info](GenApi::INodeMap& nodemap, const Pylon::CDeviceInfo&) {
                writeExposureRateNodes(nodemap, info);
            })) {
        std::cerr << "[CameraManager] applyLiveExposureRate: camera not writable for index "
                  << configArrayIndex << std::endl;
    }
}

void CameraManager::setCameraContrast(int cameraIndex, double contrast) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] setCameraContrast: invalid cameraIndex " << cameraIndex << std::endl;
        return;
    }
    
    if (cameraIndex < (int)swContrast_.size()) swContrast_[cameraIndex] = std::max(0.0, contrast);
    if (cameraIndex < (int)lutValid_.size()) lutValid_[cameraIndex] = false;
    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            GenApi::CFloatPtr ptrContrast(nodemap.GetNode("BslContrast"));
            if (ptrContrast && GenApi::IsWritable(ptrContrast)) {
                ptrContrast->SetValue(contrast);
                std::cout << "[CameraManager] setCameraContrast: cam=" << cameraIndex << " contrast=" << contrast << " OK" << std::endl;
            } else {
                std::cerr << "[CameraManager] setCameraContrast: cam " << cameraIndex << " BslContrast node not found or not writable" << std::endl;
            }
        } else {
            std::cerr << "[CameraManager] setCameraContrast: cam " << cameraIndex << " camera not open" << std::endl;
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] setCameraContrast: cam " << cameraIndex << " ERROR: " << e.GetDescription() << std::endl;
    }
}

bool CameraManager::saveParameters(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
        std::cerr << "[CameraManager] saveParameters: camera " << configArrayIndex << " not connected." << std::endl;
        return false;
    }
    // Determine the config-level ID (1-based) for a unique filename
    int configId = configArrayIndex + 1;
    if (configArrayIndex < (int)cameraIndexToConfigId_.size() && cameraIndexToConfigId_[configArrayIndex] > 0) {
        configId = cameraIndexToConfigId_[configArrayIndex];
    }
    std::string pfsDir = "/etc/papervision/cameras";
    // Ensure directory exists
    {
        QDir dir(QString::fromStdString(pfsDir));
        if (!dir.exists()) dir.mkpath(".");
    }
    std::string filename = pfsDir + "/camera_" + std::to_string(configId) + ".pfs";

    bool result = false;
    try {
        Pylon::CFeaturePersistence::Save(filename.c_str(), &camera->GetNodeMap());
        std::cout << "[CameraManager] Parameters saved to " << filename << std::endl;
        result = true;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error saving parameters: " << e.GetDescription() << std::endl;
    }
    return result;
}

bool CameraManager::loadParameters(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
        std::cerr << "[CameraManager] loadParameters: camera " << configArrayIndex << " not connected." << std::endl;
        return false;
    }
    int configId = configArrayIndex + 1;
    std::string filename = "/etc/papervision/cameras/camera_" + std::to_string(configId) + ".pfs";

    bool result = false;
    try {
        Pylon::CFeaturePersistence::Load(filename.c_str(), &camera->GetNodeMap(), true);
        std::cout << "[CameraManager] Parameters loaded from " << filename << std::endl;
        result = true;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error loading parameters: " << e.GetDescription() << std::endl;
    }
    return result;
}

void CameraManager::saveParametersForAll(const std::vector<CameraInfo>& cameras) {
    std::lock_guard<std::mutex> lock(cameraParamsMutex_);
    
    std::string pfsDir = "/etc/papervision/cameras";
    {
        QDir dir(QString::fromStdString(pfsDir));
        if (!dir.exists()) dir.mkpath(".");
    }
    
    // CFeaturePersistence::Save only reads the NodeMap — safe to call while grabbing,
    // no need to StopGrabbing which would block the UI thread.
    for (int i = 0; i < (int)cameras.size(); ++i) {
        if (cameras[i].source != 1) continue; // Only real cameras
        
        auto* camera = getCameraByConfigIndex(i);
        if (!camera) continue;
        
        if (!camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
            std::cerr << "[CameraManager] saveParametersForAll: camera " << cameras[i].id << " not connected." << std::endl;
            continue;
        }
        
        int configId = cameras[i].id;
        std::string filename = pfsDir + "/camera_" + std::to_string(configId) + ".pfs";
        
        try {
            Pylon::CFeaturePersistence::Save(filename.c_str(), &camera->GetNodeMap());
            std::cout << "[CameraManager] Parameters saved to " << filename << std::endl;
        } catch (const Pylon::GenericException& e) {
            std::cerr << "[CameraManager] Error saving parameters for cam " << cameras[i].id << ": " << e.GetDescription() << std::endl;
        }
    }
}

void CameraManager::configureCamera(GenApi::INodeMap& nodemap, const CameraInfo& config, bool isEmulation, bool preserveStartupUserSet) {
    try {
        // GenApi::INodeMap& nodemap = device->GetNodeMap(); // Removed, passed directly

        const auto setEnumIfWritable = [&nodemap](const char* nodeName, const QString& value) {
            if (value.trimmed().isEmpty()) {
                return;
            }

            try {
                GenApi::CEnumerationPtr node(nodemap.GetNode(nodeName));
                if (node && IsWritable(node)) {
                    node->FromString(value.toStdString().c_str());
                }
            } catch (const GenericException& e) {
                std::cout << "[CameraManager] Config warning: failed to set " << nodeName
                          << " to " << value.toStdString() << ": " << e.GetDescription() << std::endl;
            }
        };

        const auto setBoolIfWritable = [&nodemap](const char* nodeName, bool value) {
            try {
                GenApi::CBooleanPtr node(nodemap.GetNode(nodeName));
                if (node && IsWritable(node)) {
                    node->SetValue(value);
                }
            } catch (const GenericException& e) {
                std::cout << "[CameraManager] Config warning: failed to set " << nodeName
                          << ": " << e.GetDescription() << std::endl;
            }
        };

        const auto setFloatIfWritable = [&nodemap](const char* nodeName, double value) {
            try {
                GenApi::CFloatPtr node(nodemap.GetNode(nodeName));
                if (node && IsWritable(node)) {
                    node->SetValue(value);
                }
            } catch (const GenericException& e) {
                std::cout << "[CameraManager] Config warning: failed to set " << nodeName
                          << ": " << e.GetDescription() << std::endl;
            }
        };

        const auto setIntIfWritable = [&nodemap](const char* nodeName, int value) {
            try {
                GenApi::CIntegerPtr node(nodemap.GetNode(nodeName));
                if (node && IsWritable(node)) {
                    node->SetValue(value);
                }
            } catch (const GenericException& e) {
                std::cout << "[CameraManager] Config warning: failed to set " << nodeName
                          << ": " << e.GetDescription() << std::endl;
            }
        };

        // --- CHUNK DATA CONFIGURATION (Moved to top for verification) ---
        // 4. Enable Chunk Data (Timestamp, FrameCounter, CRC)
        if (!isEmulation) {
            // Enable Chunk Mode
            GenApi::CBooleanPtr ptrChunkModeActive(nodemap.GetNode("ChunkModeActive"));
            if (ptrChunkModeActive.IsValid()) {
                 if (IsWritable(ptrChunkModeActive)) {
                    ptrChunkModeActive->SetValue(config.chunkModeActive);
                    std::cout << "[CameraManager] Chunk Mode "
                              << (config.chunkModeActive ? "Enabled" : "Disabled") << "." << std::endl;
                 } else {
                     std::cout << "[CameraManager] ChunkModeActive found but NOT Writable." << std::endl;
                 }
            } else {
                 std::cout << "[CameraManager] ChunkModeActive Node NOT FOUND." << std::endl;
            }

            GenApi::CEnumerationPtr ptrChunkSelector(nodemap.GetNode("ChunkSelector"));
            GenApi::CBooleanPtr ptrChunkEnable(nodemap.GetNode("ChunkEnable"));

            if (IsWritable(ptrChunkSelector) && IsWritable(ptrChunkEnable)) {
                const QStringList chunkOptions = {
                    "Image",
                    "OffsetX",
                    "OffsetY",
                    "Width",
                    "Height",
                    "PixelFormat",
                    "DynamicRangeMax",
                    "DynamicRangeMin",
                    "Timestamp",
                    "Framecounter",
                    "PayloadCRC16"
                };

                for (const QString& chunk : chunkOptions) {
                    if (!GenApi::IsAvailable(ptrChunkSelector->GetEntryByName(chunk.toStdString().c_str()))) {
                        continue;
                    }

                    ptrChunkSelector->FromString(chunk.toStdString().c_str());
                    ptrChunkEnable->SetValue(config.chunkModeActive && config.enabledChunks.contains(chunk));
                }
            }
        } else {
             std::cout << "[CameraManager] Running in EMULATION mode. Skipping Chunk Mode configuration." << std::endl;
        }
        // ---------------------------------------------------------------
        
        // 0. Apply requested image geometry and pixel format within the camera's supported range.
        setEnumIfWritable("PixelFormat", config.pixelFormat);

        GenApi::CIntegerPtr offsetXNode(nodemap.GetNode("OffsetX"));
        GenApi::CIntegerPtr offsetYNode(nodemap.GetNode("OffsetY"));
        clampNodeValue(offsetXNode, config.offsetX, "OffsetX");
        clampNodeValue(offsetYNode, config.offsetY, "OffsetY");

        GenApi::CIntegerPtr widthNode(nodemap.GetNode("Width"));
        GenApi::CIntegerPtr heightNode(nodemap.GetNode("Height"));
        width_ = clampNodeValue(widthNode, config.width, "Width");
        height_ = clampNodeValue(heightNode, config.height, "Height");

        setBoolIfWritable("ExposureTimeBaseEnable", config.enableExposureTimeBase);
        setBoolIfWritable("EnableExposureTimeBase", config.enableExposureTimeBase);
        if (!preserveStartupUserSet) {
            setFloatIfWritable("ExposureTimeAbs", config.exposureTimeAbs);
            setFloatIfWritable("ExposureTime", config.exposureTimeAbs);
            setFloatIfWritable("ExposureTimeBaseAbs", config.exposureTimeBaseAbs);
            setIntIfWritable("ExposureTimeRaw", config.exposureTimeRaw);
        } else {
            std::cout << "[CameraManager] Preserving startup user set exposure values." << std::endl;
        }

        // 1. Enable PTP (IEEE 1588)
        // Note: Emulated cameras might not support this, check for existence
        GenApi::CBooleanPtr ptrPtpEnable(nodemap.GetNode("GevIEEE1588"));
        if (GenApi::IsWritable(ptrPtpEnable)) {
            ptrPtpEnable->SetValue(true);
            std::cout << "[CameraManager] PTP Enabled." << std::endl;
        }

        // 2. Persistent IP (Fixed IP)
        GenApi::CBooleanPtr ptrCurrentIpConfigPersistent(nodemap.GetNode("GevCurrentIPConfigurationPersistentIP"));
        if (IsWritable(ptrCurrentIpConfigPersistent)) {
            ptrCurrentIpConfigPersistent->SetValue(true);
        }

        if (!preserveStartupUserSet) {
            setBoolIfWritable("AcquisitionFrameRateEnable", config.enableAcquisitionFps);
            setBoolIfWritable("AcquisitionFrameRateEnabled", config.enableAcquisitionFps);
            setFloatIfWritable("AcquisitionFrameRateAbs", config.fps);
            setFloatIfWritable("AcquisitionFrameRate", config.fps);
        } else {
            std::cout << "[CameraManager] Preserving startup user set frame-rate values." << std::endl;
        }


    } catch (const GenericException& e) {
        // Ignored for emulation, but printed for debug
        std::cout << "[CameraManager] Config warning: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::startAcquisition() {
    if (acquiring_) return;

    shuttingDown_ = false;

    try {
        std::vector<CameraInfo> configuredCams = CameraConfig::getCameras();
        numCameras_ = static_cast<int>(configuredCams.size());

        // Defensive: keep all per-camera runtime containers aligned with persisted
        // configuration size. startAcquisition() may be called outside initialize().
        const size_t newCount = configuredCams.size();
        if (cameraRuntimes_.size() != newCount
            || cameraLabels_.size() != newCount
            || modelNames_.size() != newCount
            || snapshotRequests_.size() != newCount
            || swGain_.size() != newCount
            || swGamma_.size() != newCount
            || swContrast_.size() != newCount
            || lutCache_.size() != newCount
            || lutValid_.size() != newCount
            || softwareFrameCounters_.size() != newCount) {
            std::cout << "[CameraManager] Resyncing per-camera runtime vectors in startAcquisition. configured="
                      << newCount << " runtimes=" << cameraRuntimes_.size() << std::endl;

            cameraRuntimes_.resize(newCount);

            auto resizeAndFill = [&](auto& vec, auto defaultVal) {
                vec.resize(newCount, defaultVal);
            };

            resizeAndFill(cameraLabels_, std::string("Cam"));
            resizeAndFill(modelNames_, std::string("Unknown Model"));
            resizeAndFill(snapshotRequests_, false);
            resizeAndFill(swGain_, 1.0);
            resizeAndFill(swGamma_, 1.0);
            resizeAndFill(swContrast_, 1.0);
            resizeAndFill(lutCache_, cv::Mat());
            resizeAndFill(lutValid_, false);
            prevTempStatus_.resize(newCount, TemperatureStatus::Unknown);
            softwareFrameCounters_.resize(newCount, 0);

            cameraIndexToConfigId_.assign(newCount, -1);
            configArrayIndexToPylonIndex_.assign(newCount, -1);
            pylonIndexToConfigArrayIndex_.assign(newCount, -1);

            for (size_t i = 0; i < newCount; ++i) {
                cameraLabels_[i] = CameraConfig::getCameraLabel(static_cast<int>(i)).toStdString();
            }
        }

        bufferPools_.clear();
        for (const auto& cam : configuredCams) {
            bufferPools_.push_back(std::make_unique<BufferPool>(3, cam.width, cam.height, CV_8UC1));
        }

        {
            std::lock_guard<std::mutex> lock(disconnectedMutex_);
            disconnectedCameras_.clear();
        }

        {
            std::lock_guard<std::mutex> lock(latestFramesMutex_);
            latestFrames_.assign(numCameras_, cv::Mat());
        }

        // NOTE: EventController::initialize() must NOT be called here.
        // It is called from the GUI thread (MainWindow) with the correct user-configured
        // pre/post trigger values. Calling it again from this worker thread (QtConcurrent)
        // causes a data race on EventController internals and crashes the app.

        bool anyConnected = false;
        for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
            auto* camera = getCameraByConfigIndex(i);
            if (!camera) {
                continue;
            }

            if (i < 0 || i >= static_cast<int>(configuredCams.size())) {
                std::cerr << "[CameraManager] Skipping runtime slot without matching config index: " << i << std::endl;
                continue;
            }

            try {
                if (!camera->IsOpen()) {
                    camera->Open();
                }
                if (camera->GetDeviceInfo().GetDeviceClass() != "BaslerCamEmu") {
                    loadCameraDefaultUserSet(*camera);
                }

                const CameraInfo& camConfig = configuredCams[i];

                if (!anyConnected) {
                    GenApi::CIntegerPtr ptrWidth(camera->GetNodeMap().GetNode("Width"));
                    GenApi::CIntegerPtr ptrHeight(camera->GetNodeMap().GetNode("Height"));
                    if (IsReadable(ptrWidth) && IsReadable(ptrHeight)) {
                        width_ = static_cast<int>(ptrWidth->GetValue());
                        height_ = static_cast<int>(ptrHeight->GetValue());
                        std::cout << "[CameraManager] Updated resolution from camera: " << width_ << "x" << height_ << std::endl;
                    }
                }

                configureCamera(camera->GetNodeMap(), camConfig, camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu", true);
                camera->StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByUser);
                anyConnected = true;

                if (camera->IsGigE()) {
                    std::cout << "[CameraManager] Cam " << i << " Stream grabber uses "
                              << Pylon::CEnumParameter(camera->GetStreamGrabberNodeMap(), "Type").GetValueOrDefault("Other")
                              << std::endl;
                }

                try {
                    GenApi::INodeMap& nodemap = camera->GetNodeMap();
                    bool isEmu = (camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu");
                    if (!isEmu) {
                        GenApi::CEnumerationPtr ptrEvtSel(nodemap.GetNode("EventSelector"));
                        GenApi::CEnumerationPtr ptrEvtNotif(nodemap.GetNode("EventNotification"));
                        if (IsWritable(ptrEvtSel) && IsWritable(ptrEvtNotif)) {
                            if (GenApi::IsAvailable(ptrEvtSel->GetEntryByName("CriticalTemperature"))) {
                                ptrEvtSel->FromString("CriticalTemperature");
                                ptrEvtNotif->FromString("On");
                            }
                            if (GenApi::IsAvailable(ptrEvtSel->GetEntryByName("OverTemperature"))) {
                                ptrEvtSel->FromString("OverTemperature");
                                ptrEvtNotif->FromString("On");
                            }
                        }
                    }
                } catch (const GenericException& e) {
                    std::cout << "[CameraManager] Temp event setup warning (cam " << i << "): " << e.GetDescription() << std::endl;
                }
            } catch (const GenericException& e) {
                std::cerr << "[CameraManager] Failed to start camera " << i << ": " << e.GetDescription() << std::endl;
                stopCameraRuntime(i);
                {
                    std::lock_guard<std::mutex> lock(disconnectedMutex_);
                    disconnectedCameras_.insert(static_cast<uint32_t>(i));
                }
                clearCameraTile(i);
            }
        }

        prevTempStatus_.assign(std::max(numCameras_, static_cast<int>(cameraRuntimes_.size())), TemperatureStatus::Unknown);

        if (tempMonitorThread_.joinable()) {
            tempMonitorRunning_ = false;
            tempMonitorThread_.join();
        }

        tempMonitorRunning_ = true;
        tempMonitorThread_ = std::thread(&CameraManager::temperatureMonitorLoop, this);

        acquiring_ = anyConnected;
        if (anyConnected) {
            for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
                CameraRuntime& runtime = cameraRuntimes_[i];
                if (runtime.connected && runtime.camera && !runtime.grabThread.joinable()) {
                    runtime.grabThread = std::thread(&CameraManager::acquisitionLoop, this, i);
                }
            }
            std::cout << "[CameraManager] Started per-camera acquisition loops." << std::endl;
        } else {
            std::cout << "[CameraManager] Skipped acquisition loop (0 cameras attached)." << std::endl;
            if (!shuttingDown_) {
                std::cout << "[CameraManager] Bootstrapping recovery thread to poll for cameras..." << std::endl;
                startRecoveryThreadIfNeeded();
            }
        }
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Pylon exception during start: "
                  << e.GetDescription() << std::endl;
    }
}

void CameraManager::stopAcquisition() {
    std::cout << "[CameraManager] stopAcquisition begin" << std::endl;
    shuttingDown_ = true;
    acquiring_ = false;

    // Stop recovery thread first — it may try to restart acquisition
    recovering_ = false;
    std::cout << "[CameraManager] stopAcquisition joining recovery thread" << std::endl;
    joinRecoveryThread();

    // Stop temperature monitor thread
    tempMonitorRunning_ = false;
    if (tempMonitorThread_.joinable()) {
        std::cout << "[CameraManager] stopAcquisition joining temperature thread" << std::endl;
        tempMonitorThread_.join();
    }

    std::cout << "[CameraManager] stopAcquisition stopping runtimes count=" << cameraRuntimes_.size() << std::endl;
    for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
        std::cout << "[CameraManager] stopAcquisition stop runtime " << i << std::endl;
        stopCameraRuntime(i);
    }
    
    // Clear buffer pools
    bufferPools_.clear();
    std::cout << "[CameraManager] stopAcquisition complete" << std::endl;
}

void CameraManager::pauseGrabbing(bool pause) {
    paused_ = pause;
    std::cout << "[CameraManager] Grabbing " << (pause ? "PAUSED" : "RESUMED") << std::endl;
}

bool CameraManager::isGrabbingPaused() const {
    return paused_;
}

bool CameraManager::tryReconnectCamera(int configArrayIndex) {
    if (shuttingDown_) {
        return false;
    }

    auto configuredCams = CameraConfig::getCameras();
    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(configuredCams.size())) {
        return false;
    }

    stopCameraRuntime(configArrayIndex);

    CTlFactory& tlFactory = CTlFactory::GetInstance();
    DeviceInfoList_t devices;
    tlFactory.EnumerateDevices(devices);
    std::set<int> claimed;

    for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
        if (i == configArrayIndex) {
            continue;
        }

        const auto* camera = getCameraByConfigIndex(i);
        if (!camera || !camera->IsPylonDeviceAttached()) {
            continue;
        }

        for (int devIndex = 0; devIndex < static_cast<int>(devices.size()); ++devIndex) {
            if (claimed.count(devIndex)) {
                continue;
            }
            if (camera->GetDeviceInfo().GetSerialNumber() == devices[devIndex].GetSerialNumber()) {
                claimed.insert(devIndex);
                break;
            }
        }
    }

    if (!attachConfiguredCamera(configArrayIndex, configuredCams[configArrayIndex], devices, claimed, true)) {
        return false;
    }

    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return false;
    }

    try {
        if (shuttingDown_) {
            return false;
        }

        if (!camera->IsOpen()) {
            camera->Open();
        }
        if (camera->GetDeviceInfo().GetDeviceClass() != "BaslerCamEmu") {
            loadCameraDefaultUserSet(*camera);
        }
        configureCamera(camera->GetNodeMap(), configuredCams[configArrayIndex], camera->GetDeviceInfo().GetDeviceClass() == "BaslerCamEmu", true);
        camera->StartGrabbing(GrabStrategy_LatestImageOnly, GrabLoop_ProvidedByUser);
        if (shuttingDown_) {
            stopCameraRuntime(configArrayIndex);
            return false;
        }
        cameraRuntimes_[configArrayIndex].connected = true;
        cameraRuntimes_[configArrayIndex].grabThread = std::thread(&CameraManager::acquisitionLoop, this, configArrayIndex);
        setCameraFrameRate(configArrayIndex, configuredCams[configArrayIndex].fps, configuredCams[configArrayIndex].enableAcquisitionFps);
        return true;
    } catch (const GenericException& e) {
        std::cerr << "[CameraManager] Reconnect failed for camera " << configArrayIndex << ": " << e.GetDescription() << std::endl;
        stopCameraRuntime(configArrayIndex);
        return false;
    }
}

void CameraManager::recoveryLoop() {
    std::cout << "[CameraManager] --- RECOVERY LOOP STARTED ---" << std::endl;

    while (recovering_ && !shuttingDown_) {
        std::vector<int> disconnected;
        {
            std::lock_guard<std::mutex> lock(disconnectedMutex_);
            disconnected.assign(disconnectedCameras_.begin(), disconnectedCameras_.end());
        }

        bool anyRecovered = false;
        for (int configIdx : disconnected) {
            if (shuttingDown_ || !recovering_) {
                break;
            }
            if (tryReconnectCamera(configIdx)) {
                anyRecovered = true;
                std::lock_guard<std::mutex> lock(disconnectedMutex_);
                disconnectedCameras_.erase(static_cast<uint32_t>(configIdx));
            }
        }

        {
            std::lock_guard<std::mutex> lock(disconnectedMutex_);
            if (disconnectedCameras_.empty()) {
                recovering_ = false;
                if (statusCallback_) statusCallback_("[CameraManager] System Fully Recovered.");
                break;
            }
        }

        if (shuttingDown_ || !recovering_) {
            break;
        }

        if (!acquiring_) {
            bool anyConnected = false;
            for (const auto& runtime : cameraRuntimes_) {
                if (runtime.connected && runtime.camera && runtime.camera->IsGrabbing()) {
                    anyConnected = true;
                    break;
                }
            }
            acquiring_ = anyConnected;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(anyRecovered ? 100 : 500));
    }

    recovering_ = false;
    std::cout << "[CameraManager] --- RECOVERY LOOP COMPLETED ---" << std::endl;
}

// ============================================================
// Temperature Monitor Loop (background thread, per Basler App Note AW00138003000)
// ============================================================
void CameraManager::temperatureMonitorLoop() {
    std::cout << "[TempMonitor] Started. Polling every 10 seconds." << std::endl;

    while (tempMonitorRunning_) {
        // Poll each configured camera slot (config array index, 0-based)
        int numCfg = (int)configArrayIndexToPylonIndex_.size();
        if (numCfg == 0) numCfg = numCameras_;

        for (int cfgIdx = 0; cfgIdx < numCfg && tempMonitorRunning_; ++cfgIdx) {
            // Skip cameras that are currently disconnected — avoids spamming error logs
            int pylonIdx = (cfgIdx < (int)configArrayIndexToPylonIndex_.size())
                           ? configArrayIndexToPylonIndex_[cfgIdx] : -1;
            {
                std::lock_guard<std::mutex> lk(disconnectedMutex_);
                if (pylonIdx < 0 || disconnectedCameras_.count(static_cast<uint32_t>(pylonIdx)))
                    continue;
            }

            double temp = getTemperature(cfgIdx);
            TemperatureStatus status = classifyTemperature(temp);

            // Resize tracking vector if needed
            if (cfgIdx >= (int)prevTempStatus_.size()) {
                prevTempStatus_.resize(cfgIdx + 1, TemperatureStatus::Unknown);
            }

            // Fire callback only when status changes (or first known reading)
            if (status != prevTempStatus_[cfgIdx]) {
                prevTempStatus_[cfgIdx] = status;
                if (tempAlertCallback_) {
                    tempAlertCallback_(cfgIdx, temp, status);
                }
            }

            // Also always fire for Critical/Error so the UI stays updated
            if ((status == TemperatureStatus::Critical || status == TemperatureStatus::Error)
                && tempAlertCallback_) {
                tempAlertCallback_(cfgIdx, temp, status);
            }
        }

        // Sleep 10 seconds in 500ms increments so the thread is responsive to stop
        for (int t = 0; t < 20 && tempMonitorRunning_; ++t) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    std::cout << "[TempMonitor] Stopped." << std::endl;
}

void CameraManager::registerCallback(FrameCallback callback) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = callback;
}

std::vector<std::string> CameraManager::getCameraLabels() const {
    return cameraLabels_;
}

std::string CameraManager::getModelName(int configArrayIndex) {
    const auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return "Not Connected";
    }

    try {
        if (camera->IsPylonDeviceAttached()) {
            return camera->GetDeviceInfo().GetModelName().c_str();
        }
    } catch (...) {}

    if (configArrayIndex >= 0 && configArrayIndex < (int)modelNames_.size()) {
        return modelNames_[configArrayIndex];
    }
    return "Unknown Model";
}

std::string CameraManager::getIpAddress(int configArrayIndex) {
    const auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return "Offline";
    }

    try {
        if (camera->IsPylonDeviceAttached()) {
            return camera->GetDeviceInfo().GetIpAddress().c_str();
        }
    } catch (...) {}

    return "Offline";
}

cv::Size CameraManager::getCameraResolution(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return cv::Size(0, 0);
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            int w = (int)GenApi::CIntegerPtr(nodemap.GetNode("Width"))->GetValue();
            int h = (int)GenApi::CIntegerPtr(nodemap.GetNode("Height"))->GetValue();
            return cv::Size(w, h);
        }
    } catch (...) {}

    return cv::Size(0, 0);
}

double CameraManager::getCameraAcquisitionFps(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return 0.0;
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            GenApi::CFloatPtr ptrFpsAbs(nodemap.GetNode("AcquisitionFrameRateAbs"));
            if (GenApi::IsReadable(ptrFpsAbs)) {
                return ptrFpsAbs->GetValue();
            }

            GenApi::CFloatPtr ptrFps(nodemap.GetNode("AcquisitionFrameRate"));
            if (GenApi::IsReadable(ptrFps)) {
                return ptrFps->GetValue();
            }
        }
    } catch (...) {}

    return 0.0;
}

double CameraManager::getCameraFps(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return 0.0;
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            GenApi::CFloatPtr ptrFpsAbs(nodemap.GetNode("ResultingFrameRateAbs"));
            if (GenApi::IsReadable(ptrFpsAbs)) {
                return ptrFpsAbs->GetValue();
            }
            GenApi::CFloatPtr ptrFps(nodemap.GetNode("ResultingFrameRate"));
            if (GenApi::IsReadable(ptrFps)) {
                return ptrFps->GetValue();
            }
        }
    } catch (...) {}

    return 0.0;
}

double CameraManager::getTemperature(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return -1.0; // Sentinel: camera not connected
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            
            // === Attempt 1: Scout GigE (TemperatureSelector + TemperatureAbs) ===
            GenApi::CEnumerationPtr ptrTempSelector(nodemap.GetNode("TemperatureSelector"));
            if (IsWritable(ptrTempSelector)) {
                GenApi::CEnumEntryPtr ptrSensorboard(ptrTempSelector->GetEntryByName("Sensorboard"));
                if (IsReadable(ptrSensorboard)) {
                    ptrTempSelector->SetIntValue(ptrSensorboard->GetValue());
                }
            }
            GenApi::CFloatPtr ptrTempAbs(nodemap.GetNode("TemperatureAbs"));
            if (IsReadable(ptrTempAbs)) {
                return ptrTempAbs->GetValue();
            }
            
            // === Attempt 2: Ace U/L (DeviceTemperatureSelector + DeviceTemperature) ===
            GenApi::CEnumerationPtr ptrDevTempSelector(nodemap.GetNode("DeviceTemperatureSelector"));
            if (IsWritable(ptrDevTempSelector)) {
                GenApi::CEnumEntryPtr ptrCoreboard(ptrDevTempSelector->GetEntryByName("Coreboard"));
                if (IsReadable(ptrCoreboard)) {
                    ptrDevTempSelector->SetIntValue(ptrCoreboard->GetValue());
                }
            }
            GenApi::CFloatPtr ptrDevTemp(nodemap.GetNode("DeviceTemperature"));
            if (IsReadable(ptrDevTemp)) {
                return ptrDevTemp->GetValue();
            }
            
            // === Attempt 3: Try reading from Transport Layer NodeMap ===
            try {
                GenApi::INodeMap& tlNodemap = camera->GetTLNodeMap();
                GenApi::CFloatPtr ptrTlTemp(tlNodemap.GetNode("TemperatureAbs"));
                if (IsReadable(ptrTlTemp)) {
                    return ptrTlTemp->GetValue();
                }
                GenApi::CFloatPtr ptrTlDevTemp(tlNodemap.GetNode("DeviceTemperature"));
                if (IsReadable(ptrTlDevTemp)) {
                    return ptrTlDevTemp->GetValue();
                }
            } catch (...) {}
            
            // === Attempt 4: Check alternative/legacy node names ===
            const char* tempNodeNames[] = {
                "TemperatureAbs", "DeviceTemperature", "Temperature",
                "SensorBoardTemperature", "BoardTemperature",
                "BslTemperature", "DeviceTemperatureAbs",
                nullptr
            };
            for (int n = 0; tempNodeNames[n] != nullptr; ++n) {
                GenApi::CFloatPtr ptrT(nodemap.GetNode(tempNodeNames[n]));
                if (ptrT && IsReadable(ptrT)) {
                    return ptrT->GetValue();
                }
                GenApi::CIntegerPtr ptrTi(nodemap.GetNode(tempNodeNames[n]));
                if (ptrTi && IsReadable(ptrTi)) {
                    return (double)ptrTi->GetValue();
                }
            }
            
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Temperature read failed: " << e.GetDescription() << std::endl;
    }
    return -1.0; // Sentinel: unavailable
}

cv::Size CameraManager::getResolution() const {
    return cv::Size(width_, height_);
}

void CameraManager::setDefectDetectionEnabled(bool enabled) {
    defectDetectionEnabled_ = enabled;
    std::cout << "[CameraManager] Defect Detection " << (enabled ? "ENABLED" : "DISABLED") << std::endl;
}

bool CameraManager::isDefectDetectionEnabled() const {
    return defectDetectionEnabled_;
}

void CameraManager::triggerSnapshot(int cameraIndex) {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (cameraIndex >= 0 && cameraIndex < (int)snapshotRequests_.size()) {
        snapshotRequests_[cameraIndex] = true;
        std::cout << "[CameraManager] Snapshot requested for Camera " << cameraIndex << std::endl;
    }
}

void CameraManager::acquisitionLoop(int configArrayIndex) {
    auto* camera = getCameraByConfigIndex(configArrayIndex);
    if (!camera) {
        return;
    }

    if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
        std::cerr << "[CameraManager] acquisitionLoop called with invalid slot " << configArrayIndex << std::endl;
        return;
    }

    std::cout << "[CameraManager] Entering acquisition loop for slot " << configArrayIndex << std::endl;
    CGrabResultPtr ptrGrabResult;
    CImageFormatConverter formatConverter;
    formatConverter.OutputPixelFormat = PixelType_Mono8;

    CPylonImage pylonImage; 

    while (acquiring_) {
        if (configArrayIndex < 0 || configArrayIndex >= static_cast<int>(cameraRuntimes_.size())) {
            std::cerr << "[CameraManager] acquisitionLoop exiting due to runtime resize for slot "
                      << configArrayIndex << std::endl;
            break;
        }

        if (!cameraRuntimes_[configArrayIndex].connected || !camera->IsGrabbing()) {
            break;
        }

        try {
            if (camera->RetrieveResult(5000, ptrGrabResult, TimeoutHandling_Return)) {
                if (!ptrGrabResult) continue;  

                if (paused_) continue; // Skip frame processing when paused

                uint32_t cameraIndex = static_cast<uint32_t>(configArrayIndex);

                // Skip frames from cameras that have been flagged as disconnected.
                {
                    std::lock_guard<std::mutex> lock(disconnectedMutex_);
                    if (disconnectedCameras_.count(cameraIndex)) continue;
                }

                if (ptrGrabResult->GrabSucceeded()) {
                    // 1. CHUNK DATA & METADATA
                    int64_t timestamp = 0;
                    int64_t frameCounter = 0;
                    
                    bool chunkValid = false;
                    
                    if (PayloadType_ChunkData == ptrGrabResult->GetPayloadType()) {
                        if (ptrGrabResult->HasCRC() && !ptrGrabResult->CheckCRC()) {
                             std::cerr << "[CameraManager] Error: Image CRC failed!" << std::endl;
                        } else {
                            // Extract Metadata via NodeMap
                            GenApi::INodeMap& chunkNodeMap = ptrGrabResult->GetChunkDataNodeMap();
                            
                            // Timestamp. Basler GigE chunk timestamps are camera clock ticks.
                            // Convert to nanoseconds using GevTimestampTickFrequency when available
                            // (scout documentation states 8 ns ticks, i.e. 125 MHz).
                            GenApi::CIntegerPtr ptrTs(chunkNodeMap.GetNode("ChunkTimestamp"));
                            if (IsReadable(ptrTs)) {
                                const int64_t rawTimestampTicks = ptrTs->GetValue();
                                double tickFrequencyHz = 125000000.0; // safe scout GigE fallback: 1 tick = 8 ns
                                try {
                                    GenApi::INodeMap& cameraNodeMap = camera->GetNodeMap();
                                    GenApi::CIntegerPtr ptrTickFreq(cameraNodeMap.GetNode("GevTimestampTickFrequency"));
                                    if (IsReadable(ptrTickFreq) && ptrTickFreq->GetValue() > 0) {
                                        tickFrequencyHz = static_cast<double>(ptrTickFreq->GetValue());
                                    }
                                } catch (...) {}
                                timestamp = static_cast<int64_t>((static_cast<double>(rawTimestampTicks) * 1000000000.0) / tickFrequencyHz);
                            }
                            
                            // Frame Counter
                            GenApi::CIntegerPtr ptrFc(chunkNodeMap.GetNode("ChunkFramecounter"));
                            if (IsReadable(ptrFc)) {
                                frameCounter = ptrFc->GetValue();
                            }
                            
                            if (timestamp > 0) chunkValid = true;
                        }
                    } 
                    
                    // FALLBACK: If Chunk Data is missing or invalid (e.g. Emulation or timestamp=0)
                    if (!chunkValid || timestamp == 0) {
                        // Generate high-precision software timestamp (nanoseconds)
                        auto now = std::chrono::system_clock::now();
                        auto duration = now.time_since_epoch();
                        timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
                        
                        // Increment frame counter per camera using the member vector.
                        // Each slot is written exclusively by its own acquisition thread
                        // so plain int64_t is safe (no mutex or atomic needed).
                        if (cameraIndex < softwareFrameCounters_.size()) {
                            frameCounter = ++softwareFrameCounters_[cameraIndex];
                        }
                    }

                    // 2. IMAGE CONVERSION
                    const uint8_t *pImageBuffer = (uint8_t *) ptrGrabResult->GetBuffer();
                    int width = ptrGrabResult->GetWidth();
                    int height = ptrGrabResult->GetHeight();

                    if (width > 0 && height > 0 && pImageBuffer) {
                        cameraRuntimes_[configArrayIndex].consecutiveIncompleteGrabCount = 0;
                        cv::Mat displayFrame;
                        cv::Mat wrapper(height, width, CV_8UC1, (void*)pImageBuffer);
                        
                        // 3. PROCESSING
                        // Always apply per-camera software LUT (Gain/Gamma/Contrast) for visual feedback
                        cv::Mat softFrame;
                        {
                            std::lock_guard<std::mutex> lock(paramMutex_);
                            int cfgIdx = pylonIndexToConfigArrayIndex_.size() > cameraIndex
                                       ? pylonIndexToConfigArrayIndex_[cameraIndex] : (int)cameraIndex;
                            double g  = (cfgIdx >= 0 && cfgIdx < (int)swGain_.size())     ? swGain_[cfgIdx]     : 1.0;
                            double gm = (cfgIdx >= 0 && cfgIdx < (int)swGamma_.size())    ? swGamma_[cfgIdx]    : 1.0;
                            double c  = (cfgIdx >= 0 && cfgIdx < (int)swContrast_.size()) ? swContrast_[cfgIdx] : 1.0;
                            bool needsLUT = (std::abs(g-1.0) > 0.01 || std::abs(gm-1.0) > 0.01 || std::abs(c-1.0) > 0.01);
                            if (needsLUT) {
                                if (cfgIdx >= 0 && cfgIdx < (int)lutValid_.size() && lutValid_[cfgIdx]) {
                                    // Use cached LUT — avoids rebuilding 256-entry lookup + pow() on every frame
                                    cv::LUT(wrapper, lutCache_[cfgIdx], softFrame);
                                } else {
                                    // Build and cache the LUT
                                    cv::Mat lut(1, 256, CV_8U);
                                    for (int i = 0; i < 256; ++i) {
                                        double v = i * g;
                                        v = (v - 128) * c + 128;
                                        v = std::pow(std::max(v / 255.0, 0.0), 1.0 / gm) * 255.0;
                                        lut.at<uchar>(i) = static_cast<uchar>(std::min(255.0, std::max(0.0, v)));
                                    }
                                    if (cfgIdx >= 0 && cfgIdx < (int)lutCache_.size()) {
                                        lutCache_[cfgIdx] = lut;
                                        lutValid_[cfgIdx] = true;
                                    }
                                    cv::LUT(wrapper, lut, softFrame);
                                }
                            } else {
                                softFrame = wrapper; // no-op reference copy, zero overhead
                            }
                        }

                        if (defectDetectionEnabled_) {
                             processFrame(softFrame, displayFrame, (int)cameraIndex);
                        } else {
                            displayFrame = softFrame;
                        }

                        // Resolve the Config ID from the Pylon index
                        int configId = (cameraIndex < cameraIndexToConfigId_.size())
                                        ? cameraIndexToConfigId_[cameraIndex]
                                        : (int)(cameraIndex + 1); // fallback

                        // 4. EVENT CONTROLLER: feed all connected cameras, using 1-based configId
                        EventController::instance().addFrame(configId, wrapper, timestamp, frameCounter);

                        // 5. CALLBACK: emit config array index (0-based UI slot) resolved from Pylon index
                        {
                            int configArrayIdx = static_cast<int>(cameraIndex);

                            if (configArrayIdx >= 0) {
                                std::lock_guard<std::mutex> lk(latestFramesMutex_);
                                if (configArrayIdx < (int)latestFrames_.size()) {
                                    latestFrames_[configArrayIdx] = displayFrame.clone();
                                }
                            }

                            std::lock_guard<std::mutex> lock(callbackMutex_);
                            if (callback_ && configArrayIdx >= 0) {
                                callback_(configArrayIdx, displayFrame);
                            }
                        }
                    } else {
                        std::cout << "[CameraManager] Invalid frame: " << width << "x" << height << " Buffer: " << (pImageBuffer ? "OK" : "NULL") << std::endl;
                    }
                } else {
                    cameraRuntimes_[configArrayIndex].incompleteGrabCount++;
                    cameraRuntimes_[configArrayIndex].consecutiveIncompleteGrabCount++;
                    std::cerr << "[CameraManager] Grab failed: " 
                              << ptrGrabResult->GetErrorDescription() << std::endl;
                              
                    if (camera->IsCameraDeviceRemoved()) {
                        std::cerr << "[CameraManager] Hardware disconnect on camera slot " << cameraIndex << std::endl;
                        {
                            std::lock_guard<std::mutex> lock(disconnectedMutex_);
                            disconnectedCameras_.insert(cameraIndex);
                        }
                        cameraRuntimes_[configArrayIndex].connected = false;
                        int configArrayIdx = (int)cameraIndex;
                        if (configArrayIdx >= 0) {
                            std::lock_guard<std::mutex> lock(callbackMutex_);
                            if (callback_) callback_(configArrayIdx, cv::Mat());
                        }
                        if (!shuttingDown_) {
                            startRecoveryThreadIfNeeded();
                        }
                        break;
                    }
                }

                // 6. SNAPSHOT REQUESTS
                bool takeSnapshot = false;
                {
                    std::lock_guard<std::mutex> lock(snapshotMutex_);
                    if (cameraIndex < (uint32_t)snapshotRequests_.size() && snapshotRequests_[cameraIndex]) {
                        takeSnapshot = true;
                        snapshotRequests_[cameraIndex] = false; 
                    }
                }

                if (takeSnapshot) {
                    try {
                        auto now = std::chrono::system_clock::now();
                        auto time_t = std::chrono::system_clock::to_time_t(now);
                        const QString filename = QDir(CameraConfig::getEventStoragePath()).filePath(
                            QString("Snapshot_Cam%1_%2.png")
                                .arg(cameraIndex)
                                .arg(QDateTime::fromSecsSinceEpoch(time_t).toString("yyyyMMdd_HHmmss")));
                        std::cout << "[CameraManager] Saving snapshot to: " << filename.toStdString() << std::endl;
                        CImagePersistence::Save(ImageFileFormat_Png, filename.toStdString().c_str(), ptrGrabResult);
                    } catch (const GenericException& e) {
                         std::cerr << "[CameraManager] Failed to save snapshot: " << e.GetDescription() << std::endl;
                    }
                }
            }
        } catch (const GenericException& e) {
            if (acquiring_) {
                std::cerr << "[CameraManager] Pylon exception in loop: " 
                          << e.GetDescription() << std::endl;

                // Brief pause to let the OS settle after a hot-unplug event
                Pylon::WaitObject::Sleep(200);

                if (camera->IsCameraDeviceRemoved()) {
                    std::cerr << "[CameraManager] Hardware disconnect confirmed on camera slot " << configArrayIndex << std::endl;
                    {
                        std::lock_guard<std::mutex> lock(disconnectedMutex_);
                        disconnectedCameras_.insert(static_cast<uint32_t>(configArrayIndex));
                    }
                    cameraRuntimes_[configArrayIndex].connected = false;
                    clearCameraTile(configArrayIndex);
                    if (!shuttingDown_) {
                        startRecoveryThreadIfNeeded();
                    }
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[CameraManager] Standard exception in loop: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[CameraManager] Unknown exception in loop!" << std::endl;
        }
    }
    std::cout << "[CameraManager] Exiting acquisition loop for slot " << configArrayIndex << std::endl;
}

void CameraManager::processFrame(const cv::Mat& input, cv::Mat& output, int cameraIndex) {
    // Telemetry Overlay (always shown)
    std::string label = (cameraIndex < (int)cameraLabels_.size()) ? cameraLabels_[cameraIndex] : "Cam " + std::to_string(cameraIndex);
    
    // Frame Counter / Timestamp
    static std::atomic<long> frameCount{0};
    frameCount++;
    
    std::string info = label + " | F:" + std::to_string(frameCount);
    
    // OPTIMIZATION: Keep Mono8 (Grayscale) to save 3x Memory
    // Input is already Mono8.
    // Apply software Gain, Gamma, and Contrast via a LUT for performance
    {
        // Resolve config index for this pylon camera index
        int cfgIdx = cameraIndex;
        if (cameraIndex < (int)pylonIndexToConfigArrayIndex_.size()) {
            cfgIdx = pylonIndexToConfigArrayIndex_[cameraIndex];
        }
        double gain     = (cfgIdx >= 0 && cfgIdx < (int)swGain_.size())     ? swGain_[cfgIdx]     : 1.0;
        double gamma    = (cfgIdx >= 0 && cfgIdx < (int)swGamma_.size())    ? swGamma_[cfgIdx]    : 1.0;
        double contrast = (cfgIdx >= 0 && cfgIdx < (int)swContrast_.size()) ? swContrast_[cfgIdx] : 1.0;

        bool needsProcessing = (std::abs(gain - 1.0) > 0.01 || std::abs(gamma - 1.0) > 0.01 || std::abs(contrast - 1.0) > 0.01);
        if (needsProcessing) {
            // Build a 256-entry LUT: Gain -> Contrast -> Gamma
            cv::Mat lut(1, 256, CV_8U);
            for (int i = 0; i < 256; ++i) {
                double v = i * gain;                // apply gain
                v = (v - 128) * contrast + 128;    // apply contrast around midpoint
                v = std::pow(std::max(v / 255.0, 0.0), 1.0 / gamma) * 255.0; // apply gamma
                lut.at<uchar>(i) = static_cast<uchar>(std::min(255.0, std::max(0.0, v)));
            }
            cv::LUT(input, lut, output);
        } else {
            if (input.data != output.data) {
                input.copyTo(output);
            }
        }
    }
    
    // Only run defect detection if enabled
    if (defectDetectionEnabled_) {
        // "Best Result": Convert Mono8 to BGR to allow colored (RED) defect visualization
        // converting input (Mono8) to gray for processing
        cv::Mat gray, processed;
        
        // Input is already Mono8 (guaranteed by buffer pool)
        gray = input; // Soft copy
        
        // 1. Noise Reduction
        cv::GaussianBlur(gray, processed, cv::Size(5, 5), 1.5);
        
        // 2. Defect Detection (Adaptive Threshold)
        cv::adaptiveThreshold(processed, processed, 255, 
            cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY_INV, 11, 2);
            
        // 3. Web Edge Stability (Canny)
        cv::Canny(processed, processed, 50, 150);
        
        // Find contours
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(processed, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        // Draw significant contours (potential defects)
        for (const auto& contour : contours) {
            double area = cv::contourArea(contour);
            if (area > 100) { // Filter small noise
                 // Draw in WHITE (255) for Mono8 Optimized Result
                 cv::drawContours(output, std::vector<std::vector<cv::Point>>{contour}, -1, cv::Scalar(255), 2);
                 
                 // SIMULATED TRIGGER
                 if (area > 5000) {
                     EventController::TriggerContext triggerContext;
                     triggerContext.reason = QStringLiteral("Defect Detection");
                     triggerContext.source = QStringLiteral("defect");
                     // The defect is physically at this camera's machine position:
                     // spatial alignment centers every camera's window on when the
                     // defect passes it (offset = (P_cam - P_detect) / speed).
                     if (cameraIndex >= 0) {
                         triggerContext.triggerPositionMm =
                             CameraConfig::getCameraInfo(cameraIndex).machinePosition;
                     }
                     EventController::instance().triggerEvent(triggerContext);
                     cv::putText(output, "TRIGGERED!", cv::Point(10, 80), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255), 2);
                 }
            }
        }
        
        info += " | Defect Scan: ACTIVE";
    } else {
        // Defect Scan OFF
        info += " | Defect Scan: OFF";
    }
    // removed overlay drawing
}


void CameraManager::setGlobalFrameRate(double fps) {
    fps_ = static_cast<int>(fps);
    std::cout << "[CameraManager] Setting global FPS to " << fps << std::endl;

    try {
        for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
            auto* camera = getCameraByConfigIndex(i);
            if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
                continue;
            }

            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            try {
                Pylon::CFloatParameter(nodemap, "AcquisitionFrameRate").SetValue(fps);
            } catch (...) {
                std::cerr << "[CameraManager] Could not set AcquisitionFrameRate on camera " << i << std::endl;
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during FPS set: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setCameraFrameRate(int cameraIndex, double fps, bool enableFrameRate) {
    std::cout << "[CameraManager] Setting FPS for Camera " << cameraIndex << " to " << fps
              << " (enable=" << enableFrameRate << ")" << std::endl;

    auto* camera = getCameraByConfigIndex(cameraIndex);
    if (!camera) {
        std::cerr << "[CameraManager] Invalid camera index for FPS update: " << cameraIndex << std::endl;
        return;
    }

    try {
        if (camera->IsPylonDeviceAttached() && camera->IsOpen()) {
            GenApi::INodeMap& nodemap = camera->GetNodeMap();

            GenApi::CBooleanPtr enableNode(nodemap.GetNode("AcquisitionFrameRateEnable"));
            if (!enableNode || !GenApi::IsWritable(enableNode)) {
                enableNode = GenApi::CBooleanPtr(nodemap.GetNode("AcquisitionFrameRateEnabled"));
            }
            if (enableNode && GenApi::IsWritable(enableNode)) {
                enableNode->SetValue(enableFrameRate);
            }

            if (enableFrameRate) {
                GenApi::CFloatPtr fpsNode(nodemap.GetNode("AcquisitionFrameRateAbs")); // Basler scout / older SFNC
                if (!fpsNode || !GenApi::IsWritable(fpsNode)) {
                    fpsNode = GenApi::CFloatPtr(nodemap.GetNode("AcquisitionFrameRate")); // newer SFNC
                }

                if (fpsNode && GenApi::IsWritable(fpsNode)) {
                    const double clamped = std::max(fpsNode->GetMin(), std::min(fps, fpsNode->GetMax()));
                    fpsNode->SetValue(clamped);
                } else {
                    std::cerr << "[CameraManager] Could not set AcquisitionFrameRateAbs/AcquisitionFrameRate on camera "
                              << cameraIndex << std::endl;
                }
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during individual FPS set: " << e.GetDescription() << std::endl;
    }
}

void CameraManager::setGlobalResolution(int binningFactor) {
    std::cout << "[CameraManager] Setting global Binning to " << binningFactor << "x" << binningFactor << std::endl;

    try {
        for (int i = 0; i < static_cast<int>(cameraRuntimes_.size()); ++i) {
            auto* camera = getCameraByConfigIndex(i);
            if (!camera || !camera->IsPylonDeviceAttached() || !camera->IsOpen()) {
                continue;
            }

            GenApi::INodeMap& nodemap = camera->GetNodeMap();
            try {
                if (Pylon::CIntegerParameter(nodemap, "BinningHorizontal").IsWritable()) {
                    Pylon::CIntegerParameter(nodemap, "BinningHorizontal").SetValue(binningFactor);
                }

                if (Pylon::CIntegerParameter(nodemap, "BinningVertical").IsWritable()) {
                    Pylon::CIntegerParameter(nodemap, "BinningVertical").SetValue(binningFactor);
                }
            } catch (...) {
                std::cerr << "[CameraManager] Binning not supported on camera " << i << std::endl;
            }
        }
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Pylon Error during Binning set: " << e.GetDescription() << std::endl;
    }
}

std::vector<GigEDeviceInfo> CameraManager::enumerateGigEDevices(bool forceRefresh) {
    // GigE enumeration is a network broadcast scan that can block for seconds,
    // especially when no cameras are present. Cache the result briefly so the
    // Device Settings dialog (which polls reachability multiple times per
    // refresh cycle) doesn't hammer the wire on every poll.
    //
    // NOTE: empty results are cached too — the offline/no-camera case is the
    // slow one, so it must not re-scan on every reachability check.
    static std::mutex cacheMutex;
    static std::vector<GigEDeviceInfo> cachedDevices;
    static std::chrono::steady_clock::time_point cachedAt;
    static bool cacheValid = false;
    constexpr auto kCacheTtl = std::chrono::milliseconds(2500);

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto now = std::chrono::steady_clock::now();
        if (!forceRefresh && cacheValid && (now - cachedAt) < kCacheTtl) {
            return cachedDevices;
        }
    }

    std::vector<GigEDeviceInfo> devices;
    try {
        Pylon::CTlFactory& TlFactory = Pylon::CTlFactory::GetInstance();
        Pylon::IGigETransportLayer* pTl = dynamic_cast<Pylon::IGigETransportLayer*>(TlFactory.CreateTl(Pylon::BaslerGigEDeviceClass));
        if (pTl == nullptr) {
            std::cerr << "[CameraManager] Error: No GigE transport layer installed." << std::endl;
            return devices;
        }

        Pylon::DeviceInfoList_t lstDevices;
        pTl->EnumerateAllDevices(lstDevices);

        for (const auto& dev : lstDevices) {
            GigEDeviceInfo info;
            info.friendlyName = dev.GetFriendlyName().c_str();
            info.macAddress = dev.GetMacAddress().c_str();
            
            Pylon::String_t val;
            if (dev.GetPropertyValue("IpAddress", val)) info.ipAddress = val.c_str();
            if (dev.GetPropertyValue("SubnetMask", val)) info.subnetMask = val.c_str();
            if (dev.GetPropertyValue("DefaultGateway", val)) info.defaultGateway = val.c_str();
            if (dev.GetPropertyValue("UserDefinedName", val)) info.userDefinedName = val.c_str();

            if (dev.IsPersistentIpActive())      info.ipConfigMode = "Static";
            else if (dev.IsDhcpActive())         info.ipConfigMode = "DHCP";
            else                                 info.ipConfigMode = "AutoIP";
            info.supportsPersistentIp = dev.IsPersistentIpSupported();
            info.supportsDhcp         = dev.IsDhcpSupported();
            info.supportsAutoIp       = dev.IsAutoIpSupported();
            
            devices.push_back(info);
        }
        
        TlFactory.ReleaseTl(pTl);
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error enumerating GigE devices: " << e.GetDescription() << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedDevices = devices;
        cachedAt = std::chrono::steady_clock::now();
        cacheValid = true;
    }
    return devices;
}

IpConfigResult CameraManager::configureIpConfiguration(const std::string& mac, const std::string& mode,
                                                       const std::string& ip, const std::string& mask,
                                                       const std::string& gateway) {
    const bool isStatic = (mode == "Static");
    const bool isDhcp   = (mode == "DHCP");
    const bool isAuto   = (mode == "AutoIP");
    if (!isStatic && !isDhcp && !isAuto) {
        std::cerr << "[CameraManager] Unknown IP config mode: " << mode << std::endl;
        return IpConfigResult::WriteFailed;
    }
    try {
        Pylon::CTlFactory& TlFactory = Pylon::CTlFactory::GetInstance();
        Pylon::IGigETransportLayer* pTl = dynamic_cast<Pylon::IGigETransportLayer*>(TlFactory.CreateTl(Pylon::BaslerGigEDeviceClass));
        if (pTl == nullptr) {
            std::cerr << "[CameraManager] Error: No GigE transport layer installed." << std::endl;
            return IpConfigResult::WriteFailed;
        }

        // Find user defined name and validate device exists. Retry discovery:
        // GigE discovery can miss cameras on busy networks or while a camera
        // is restarting its network stack (2-10 s after an IP change).
        const std::string targetMac = normalizeMacAddress(mac);
        Pylon::DeviceInfoList_t lstDevices;
        std::string userDefinedName = "";
        std::string currentIp;
        Pylon::CDeviceInfo matchedDeviceInfo;
        bool found = false;
        for (int attempt = 0; attempt < 5 && !found; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            lstDevices.clear();
            pTl->EnumerateAllDevices(lstDevices);
            for (const auto& dev : lstDevices) {
                const std::string enumeratedMac = normalizeMacAddress(dev.GetMacAddress().c_str());
                if (enumeratedMac == targetMac) {
                    found = true;
                    matchedDeviceInfo = dev;
                    userDefinedName = dev.GetUserDefinedName().c_str();
                    Pylon::String_t val;
                    if (dev.GetPropertyValue("IpAddress", val)) {
                        currentIp = val.c_str();
                    }
                    break;
                }
            }
        }

        if (!found) {
            std::cerr << "[CameraManager] Cannot apply IP config: target MAC " << mac
                      << " was not found after 5 discovery attempts (1 s apart). Visible devices:";
            for (const auto& dev : lstDevices) {
                std::cerr << " " << normalizeMacAddress(dev.GetMacAddress().c_str());
            }
            std::cerr << std::endl;
            TlFactory.ReleaseTl(pTl);
            return IpConfigResult::DeviceNotFound;
        }

        std::cout << "[CameraManager] Applying GigE IP config: MAC=" << targetMac
                  << " mode=" << mode
                  << " currentIp=" << (currentIp.empty() ? "<unknown>" : currentIp)
                  << " targetIp=" << ip
                  << " mask=" << mask
                  << " gateway=" << gateway << std::endl;

        // A camera stuck in AutoIP mode (current IP 169.254.x.x) is not
        // reachable from the host subnet. The scA780 family ignores the GVCP
        // force-IP broadcast (BroadcastIpConfiguration returns false), so the
        // only working path is to temporarily add a link-local (169.254.0.0/16)
        // address to the camera NIC — the "assign temporary IP" mechanism. This
        // makes the camera's AutoIP address routable so the direct device API
        // below can open it and write the real static configuration. The
        // address is added as a SECONDARY address (never replaces the primary
        // host IP) and is removed again once the write completes.
        LinkLocalBridgeGuard linkLocalBridge;
        if (currentIp.rfind("169.254.", 0) == 0 && !hasLinkLocalAddress()) {
            const int ifindex = interfaceIndexContaining(ip);
            if (ifindex >= 0) {
                const std::string llAddr = linkLocalAddressForMac(targetMac);
                if (setSecondaryIpv4Address(ifindex, llAddr, 16, /*add=*/true)) {
                    linkLocalBridge.ifindex = ifindex;
                    linkLocalBridge.addr = llAddr;
                    linkLocalBridge.active = true;
                    std::cout << "[CameraManager] AutoIP camera " << targetMac
                              << " not reachable; added temporary link-local address "
                              << llAddr << "/16 (secondary) on interface index "
                              << ifindex << " to write the IP configuration." << std::endl;
                } else {
                    std::cerr << "[CameraManager] Failed to add temporary link-local address for camera "
                              << targetMac << "; direct API will likely time out." << std::endl;
                }
            } else {
                std::cerr << "[CameraManager] No host interface matches the target subnet for camera "
                          << targetMac << "; cannot reach an AutoIP camera." << std::endl;
            }
        }

        // Direct device API: the reliable path on cameras that ignore GVCP
        // broadcast IP configuration (e.g. scA780). It writes the persistent
        // IP settings and switches the configuration mode on the device itself.
        //
        // Newer camera models restart their network interface IMMEDIATELY when
        // the configuration is changed, which can make the final Close() fail
        // (device unreachable mid-teardown). The apply has already gone through
        // in that case, so a teardown failure after a successful write must NOT
        // be reported as an apply failure.
        auto tryDirect = [&](const Pylon::CDeviceInfo& devInfo, bool* applied) -> bool {
            *applied = false;
            try {
                std::unique_ptr<Pylon::IPylonDevice> device(TlFactory.CreateDevice(devInfo));
                Pylon::CBaslerGigEInstantCamera camera(device.release());
                camera.Open();
                if (isStatic) {
                    camera.SetPersistentIpAddress(ip.c_str(), mask.c_str(), gateway.c_str());
                }
                camera.ChangeIpConfiguration(isStatic, isDhcp);
                *applied = true; // config write succeeded; camera may reboot now
                camera.Close();
                std::cout << "[CameraManager] Successfully changed IP config for MAC " << targetMac
                          << " mode=" << mode << (isStatic ? (" to " + ip) : "")
                          << " using direct GigE device API." << std::endl;
                return true;
            } catch (const Pylon::GenericException& e) {
                std::cerr << "[CameraManager] Direct GigE IP configuration attempt for MAC " << targetMac
                          << " failed: " << e.GetDescription() << std::endl;
                // Close() failure after a successful write means the camera
                // restarted its network stack — expected, not an error.
                return *applied;
            }
        };

        // Retry the direct path: right after a mode/IP change the camera
        // restarts its network stack and is unreachable for a few seconds.
        // Re-discover the device by MAC on every attempt — after the first
        // write the camera may answer at a NEW address, and reusing the stale
        // device info (captured at the old address) would make every retry fail.
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }

            if (attempt > 0) {
                lstDevices.clear();
                pTl->EnumerateAllDevices(lstDevices);
                bool rediscovered = false;
                for (const auto& dev : lstDevices) {
                    if (normalizeMacAddress(dev.GetMacAddress().c_str()) == targetMac) {
                        matchedDeviceInfo = dev;
                        rediscovered = true;
                        break;
                    }
                }
                if (!rediscovered) {
                    // Camera is still restarting its network stack; wait for it.
                    continue;
                }
            }

            bool applied = false;
            if (tryDirect(matchedDeviceInfo, &applied)) {
                // Mirror Utility_IpConfig: restart the camera's IP stack so the
                // new persistent configuration takes effect on the LIVE
                // interface (scA780 does not apply it otherwise).
                pTl->RestartIpConfiguration(targetMac.c_str());
                std::cout << "[CameraManager] IP stack restart triggered for MAC " << targetMac << std::endl;
                TlFactory.ReleaseTl(pTl);
                return IpConfigResult::Success;
            }
        }

        // Broadcast fallback: pylon SDK requires the MAC with NO delimiters
        // (e.g., "003053061a58") and the camera must NOT be open. Only some
        // cameras accept broadcast IP configuration.
        bool setOk = pTl->BroadcastIpConfiguration(
            targetMac.c_str(), isStatic, isDhcp,
            isStatic ? ip.c_str() : "0.0.0.0",
            isStatic ? mask.c_str() : "0.0.0.0",
            isStatic ? gateway.c_str() : "0.0.0.0",
            userDefinedName.c_str());

        if (setOk) {
            pTl->RestartIpConfiguration(targetMac.c_str());
            std::cout << "[CameraManager] Successfully changed IP config for MAC " << targetMac
                      << " mode=" << mode << (isStatic ? (" to " + ip) : "") << std::endl;
        } else {
            std::cerr << "[CameraManager] Failed to change IP config for MAC " << targetMac
                      << " mode=" << mode << " (input=" << mac << ")" << std::endl;
        }

        TlFactory.ReleaseTl(pTl);
        return setOk ? IpConfigResult::Success : IpConfigResult::WriteFailed;
    } catch (const Pylon::GenericException& e) {
        std::cerr << "[CameraManager] Error applying IP config: " << e.GetDescription() << std::endl;
        return IpConfigResult::WriteFailed;
    }
}
