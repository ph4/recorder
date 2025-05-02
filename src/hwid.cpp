#include "hwid.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

#include <atlbase.h>
#include <atlconv.h>
#include <combaseapi.h>
#include <wbemcli.h>
#include <wil/com.h>
#include <windows.h>

#include <hmac_sha256.h>
#include <sha256.h>
#include <spdlog/spdlog.h>

#include "util.hpp"

// Should be called only once
[[nodiscard]] int WmiConnection::init_security() {
    // Set general COM security levels
    auto hr = CoInitializeSecurity(
          nullptr,
          -1,                          // COM authentication
          nullptr,                     // Authentication services
          nullptr,                     // Reserved
          RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication
          RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation
          nullptr,                     // Authentication info
          EOAC_NONE,                   // Additional capabilities
          nullptr                      // Reserved
    );

    if (FAILED(hr)) {
        SPDLOG_ERROR("CoInitializeSecurity failed. {}", hresult_to_string(hr));
        return hr;
    }
    return 0;
}

[[nodiscard]] int WmiConnection::create_services(const std::string &path) {
    wil::com_ptr_nothrow<IWbemLocator> loc;
    HRESULT hr = CoCreateInstance(
          CLSID_WbemLocator,
          nullptr,
          CLSCTX_INPROC_SERVER,
          IID_IWbemLocator,
          reinterpret_cast<LPVOID *>(&loc)
    );

    if (FAILED(hr)) {
        SPDLOG_ERROR("Failed to create IWbemLocator {}", hresult_to_string(hr));
        return hr;
    }

    // Connect to WMI through the IWbemLocator::ConnectServer method
    const auto bstr_path = wil::make_bstr(CA2W(path.c_str()));
    // Connect to the namespace
    // and obtain pointer pSvc to make IWbemServices calls.
    hr = loc->ConnectServer(
          bstr_path.get(),
          wil::unique_bstr().get(),
          wil::unique_bstr().get(),
          wil::unique_bstr().get(),
          WBEM_FLAG_CONNECT_USE_MAX_WAIT,
          wil::unique_bstr().get(),
          nullptr,
          &this->svc
    );

    if (this->svc.get() == (IWbemServices *)-1) {
        throw std::runtime_error("Failed to connect to WMI (ptr = -1)");
    }

    if (FAILED(hr)) {
        SPDLOG_ERROR("Could not connect. {}", hresult_to_string((hr)));
        return hr;
    }

    SPDLOG_DEBUG("Connected to {} WMI namespace", path.c_str());

    // Set security levels on the proxy
    hr = CoSetProxyBlanket(
          svc.get(),                   // Indicates the proxy to set
          RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
          RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
          nullptr,                     // Server principal name
          RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx
          RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
          nullptr,                     // client identity
          EOAC_NONE
    ); // proxy capabilities

    if (FAILED(hr)) {
        SPDLOG_ERROR("Could not set proxy blanket. {}", hresult_to_string(hr));
        return hr;
    }
    SPDLOG_DEBUG("Set proxy blanket");
    return 0;
}
using QueryResults = std::vector<std::map<std::string, std::string>>;

[[nodiscard]] int WmiConnection::do_query(const std::string &query, QueryResults &result) const {
    // Use the IWbemServices pointer to make requests of WMI ----
    wil::com_ptr_nothrow<IEnumWbemClassObject> enumerator;
    auto hr = svc->ExecQuery(
          wil::make_bstr(L"WQL").get(),
          wil::make_bstr(CA2W(query.c_str())).get(),
          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
          nullptr,
          &enumerator
    );

    if (FAILED(hr)) {
        SPDLOG_ERROR("Could not execute query {}", hresult_to_string(hr));
        return hr;
    }

    while (enumerator) {
        wil::com_ptr_nothrow<IWbemClassObject> clobj;
        std::map<std::string, std::string> objs;

        ULONG uReturn = 0;
        hr = enumerator->Next(WBEM_INFINITE, 1, &clobj, &uReturn);
        RETURN_IF_FAILED(hr);
        if (uReturn == 0) break; // Enumeration endedq

        VARIANT vtProp;
        VariantInit(&vtProp);
        RETURN_IF_FAILED(clobj->BeginEnumeration(0));
        while (hr == 0) {
            wil::unique_bstr name;
            CIMTYPE type;
            tag_WBEM_FLAVOR_TYPE flavor;
            hr = clobj->Next(0, &name, &vtProp, &type, reinterpret_cast<long *>(&flavor));
            if (flavor == WBEM_FLAVOR_ORIGIN_SYSTEM) continue;

            if (hr == WBEM_S_NO_MORE_DATA) break;
            if (hr) return hr;
            auto val = vtProp.bstrVal ? std::string(CW2A(vtProp.bstrVal)) : std::string();
            objs.emplace(std::make_pair(std::string(CW2A(name.get())), val));
        }
        RETURN_IF_FAILED(clobj->EndEnumeration());
        RETURN_IF_FAILED(VariantClear(&vtProp));
        result.emplace_back(std::move(objs));
    }

    return 0;
}

int WmiConnection::create_wmi() {
    thread_local bool co_inited = false;
    int hr;
    if (!co_inited) {
        hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) {
            return hr;
        }
        co_inited = true;
    }
    static bool sec_inited = false;
    if (!sec_inited) {
        hr = init_security();
        if (hr) return hr;
        sec_inited = true;
    }
    hr = create_services();
    if (hr) return hr;
    return 0;
}

std::string get_uuid() {
    std::string key = "cpp-recorder";
    std::vector<byte> data{};

    SPDLOG_TRACE("Creating WMI connection...");
    WmiConnection connection;
    if (const auto hr = connection.create_wmi(); FAILED(hr)) {
        throw std::runtime_error("Failed to create WMI connection.");
    }
    SPDLOG_TRACE("Getting disk serial...");
    WmiConnection::QueryResults results;
    auto hr = connection.do_query("SELECT SerialNumber FROM Win32_DiskDrive", results);
    if (hr) {
        SPDLOG_ERROR("Getting disk serial failed. {}", hresult_to_string(hr));
        throw std::runtime_error("Getting disk serial failed.");
    }
    auto serial = results[0].at("SerialNumber");
    data.insert(data.end(), serial.begin(), serial.end());

    SPDLOG_TRACE("Getting UUID from Win32_ComputerSystemProduct...");
    results.clear();
    hr = connection.do_query("SELECT UUID from Win32_ComputerSystemProduct", results);
    if (hr) {
        SPDLOG_ERROR(
              "Getting UUID from Win32_ComputerSystemProduct failed. {}", hresult_to_string(hr)
        );
        throw std::runtime_error("Getting UUID from Win32_ComputerSystemProduct failed.");
    }
    assert(!results.empty());
    auto uuid = results[0].at("UUID");
    SPDLOG_DEBUG("csproduct UUID: {}", uuid);
    data.insert(data.end(), uuid.begin(), uuid.end());

    std::array<byte, SHA256_HASH_SIZE> out{};
    hmac_sha256(key.data(), key.size(), data.data(), data.size(), out.data(), out.size());

    std::string res(36, 'X');
    auto it = res.begin();
    for (auto i = 0; i < 16; i++) {
        const auto hex = "0123456789abcdef";
        const auto b = out[i * 2];
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            *(it++) = '-';
        }
        *(it++) = hex[b >> 4 & static_cast<byte>(0xf)];
        *(it++) = hex[b >> 0 & static_cast<byte>(0xf)];
    }
    return res;
}
