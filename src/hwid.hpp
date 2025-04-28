//
// Created by pavel on 02.12.2024.
//

#ifndef HWID_HPP
#define HWID_HPP

#include <string>
#include <vector>
#include <map>

#include <windows.h>
#include <wbemcli.h>
#include <wil/com.h>


class WmiConnection {
public:
    wil::com_ptr_nothrow<IWbemServices> svc;

    //Should be called only once
    [[nodiscard]] static int init_security();

    [[nodiscard]] int create_services(const std::string &path = "ROOT\\CIMV2");
    using QueryResults = std::vector<std::map<std::string, std::string>>;

    [[nodiscard]]
    int do_query(const std::string &query, QueryResults &result) const;

    int create_wmi();

    // ~WmiConnection() {
    //     svc->Release();
    // }
};


std::string get_uuid();

#endif //HWID_HPP
