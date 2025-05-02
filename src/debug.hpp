//
// Created by pavel on 31.12.2024.
//

#include <windows.h>

#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>

#include <spdlog/spdlog.h>
#include <wil/com.h>
#include <wrl/client.h>

#include "util.hpp"

namespace recorder::debug {
    inline void print_all_endpoints() {
        CoInitializeGuard coInitGuard;

        wil::com_ptr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(
              __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)
        );
        if (FAILED(hr)) {
            throw HrError("Failed to create IMMDeviceEnumerator.", hr);
        }

        auto print_endpoints = [&](EDataFlow flow) {
            wil::com_ptr<IMMDeviceCollection> collection;
            enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
            UINT32 count = 0;
            hr = collection->GetCount(&count);
            for (DWORD i = 0; i < count; i++) {
                wil::com_ptr_nothrow<IMMDevice> device;
                collection->Item(i, &device);
                wchar_t* id;
                device->GetId(&id);

                char id2[MAX_PATH];
                wcstombs_s(nullptr, id2, MAX_PATH, id, MAX_PATH);
                spdlog::debug("  Device ID: {}", id2);
            }
        };

        auto print_default_endpoints = [&](EDataFlow flow, const char* tag) {
            spdlog::info("Default {} Devices :", tag);

            auto print_endpoint = [&](ERole role, const char* tag) {
                wchar_t* id;
                char id2[MAX_PATH];
                wil::com_ptr_nothrow<IMMDevice> device;
                enumerator->GetDefaultAudioEndpoint(flow, role, &device);
                device->GetId(&id);
                wcstombs_s(nullptr, id2, MAX_PATH, id, MAX_PATH);
                spdlog::debug(" {} : {}", tag, id2);
            };
            print_endpoint(eConsole, "Console");
            print_endpoint(eMultimedia, "Multimedia");
            print_endpoint(eCommunications, "Communications");
        };

        print_default_endpoints(eRender, "Render");
        print_default_endpoints(eCapture, "Capture");

        spdlog::info("Render Devices:");
        print_endpoints(eRender);
        spdlog::info("Capture Devices:");
        print_endpoints(eCapture);
    }

} // namespace recorder::debug
