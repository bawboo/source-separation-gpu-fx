#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

struct AdapterInfo {
    std::string name;
    std::uint32_t vendorId{};
    std::uint32_t deviceId{};
    std::uint64_t dedicatedVideoMemory{};
    bool software{};
};

struct CudaInfo {
    bool driverLibraryPresent{};
    bool usable{};
    int driverVersion{};
    int deviceCount{};
    std::string firstDeviceName;
    int initializationResult{-1};
};

std::string utf8(const wchar_t* value) {
    if (value == nullptr || value[0] == L'\0') {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        -1,
        result.data(),
        required,
        nullptr,
        nullptr);
    result.pop_back();
    return result;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character) << std::dec;
            } else {
                output << static_cast<char>(character);
            }
        }
    }
    return output.str();
}

std::vector<AdapterInfo> enumerateAdapters() {
    std::vector<AdapterInfo> adapters;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return adapters;
    }
    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description))) {
            continue;
        }
        adapters.push_back(AdapterInfo{
            utf8(description.Description),
            description.VendorId,
            description.DeviceId,
            static_cast<std::uint64_t>(description.DedicatedVideoMemory),
            (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0});
    }
    return adapters;
}

CudaInfo probeCudaDriver() {
    CudaInfo info;
    const HMODULE module = LoadLibraryW(L"nvcuda.dll");
    if (module == nullptr) {
        return info;
    }
    info.driverLibraryPresent = true;

    using CuInit = int(__stdcall*)(unsigned int);
    using CuDriverGetVersion = int(__stdcall*)(int*);
    using CuDeviceGetCount = int(__stdcall*)(int*);
    using CuDeviceGetName = int(__stdcall*)(char*, int, int);
    const auto cuInit = reinterpret_cast<CuInit>(GetProcAddress(module, "cuInit"));
    const auto cuDriverGetVersion = reinterpret_cast<CuDriverGetVersion>(
        GetProcAddress(module, "cuDriverGetVersion"));
    const auto cuDeviceGetCount = reinterpret_cast<CuDeviceGetCount>(
        GetProcAddress(module, "cuDeviceGetCount"));
    const auto cuDeviceGetName = reinterpret_cast<CuDeviceGetName>(
        GetProcAddress(module, "cuDeviceGetName"));

    if (cuInit != nullptr && cuDriverGetVersion != nullptr &&
        cuDeviceGetCount != nullptr && cuDeviceGetName != nullptr) {
        info.initializationResult = cuInit(0);
        if (info.initializationResult == 0) {
            if (cuDriverGetVersion(&info.driverVersion) != 0) {
                info.driverVersion = 0;
            }
            if (cuDeviceGetCount(&info.deviceCount) != 0) {
                info.deviceCount = 0;
            }
            if (info.deviceCount > 0) {
                char name[256]{};
                if (cuDeviceGetName(name, static_cast<int>(sizeof(name)), 0) == 0) {
                    info.firstDeviceName = name;
                }
                info.usable = true;
            }
        }
    }
    FreeLibrary(module);
    return info;
}

std::string makeJson(const std::vector<AdapterInfo>& adapters, const CudaInfo& cuda) {
    bool nvidiaAdapterPresent = false;
    for (const auto& adapter : adapters) {
        nvidiaAdapterPresent = nvidiaAdapterPresent ||
                               (!adapter.software && adapter.vendorId == 0x10de);
    }

    std::ostringstream output;
    output << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"nvidia_adapter_present\": "
           << (nvidiaAdapterPresent ? "true" : "false") << ",\n"
           << "  \"cuda_driver_library_present\": "
           << (cuda.driverLibraryPresent ? "true" : "false") << ",\n"
           << "  \"cuda_usable\": " << (cuda.usable ? "true" : "false") << ",\n"
           << "  \"cuda_initialization_result\": " << cuda.initializationResult << ",\n"
           << "  \"cuda_driver_version\": " << cuda.driverVersion << ",\n"
           << "  \"cuda_device_count\": " << cuda.deviceCount << ",\n"
           << "  \"cuda_device_name\": \"" << jsonEscape(cuda.firstDeviceName)
           << "\",\n"
           << "  \"recommended_runtime\": \""
           << (cuda.usable ? "cuda" : "cpu") << "\",\n"
           << "  \"adapters\": [";
    for (std::size_t index = 0; index < adapters.size(); ++index) {
        const auto& adapter = adapters[index];
        output << (index == 0 ? "\n" : ",\n")
               << "    {\"name\": \"" << jsonEscape(adapter.name)
               << "\", \"vendor_id\": " << adapter.vendorId
               << ", \"device_id\": " << adapter.deviceId
               << ", \"dedicated_video_memory_bytes\": "
               << adapter.dedicatedVideoMemory
               << ", \"software\": " << (adapter.software ? "true" : "false")
               << "}";
    }
    output << (adapters.empty() ? "" : "\n  ") << "]\n}\n";
    return output.str();
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        std::filesystem::path outputPath;
        for (int index = 1; index < argc; ++index) {
            if (std::wstring_view(argv[index]) == L"--json" && index + 1 < argc) {
                outputPath = argv[++index];
            }
        }
        const auto document = makeJson(enumerateAdapters(), probeCudaDriver());
        if (outputPath.empty()) {
            std::cout << document;
        } else {
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            if (!output) {
                std::cerr << "Unable to create probe report\n";
                return 2;
            }
            output << document;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Hardware probe failed: " << error.what() << '\n';
        return 1;
    }
}
