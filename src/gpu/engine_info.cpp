﻿/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#include "engine_info.h"
#include "ocl_toolkit.h"
#include <unordered_map>
#include <string>
#include <cassert>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <cstring>
#elif defined(__linux__)
#include <fstream>
#endif

namespace neural { namespace gpu{

namespace {

const char* device_info_failed_msg = "Device lookup failed";

int get_gpu_device_id()
{
    int result = 0;

#ifdef _WIN32
    {
        HDEVINFO device_info_set = SetupDiGetClassDevsA(&GUID_DEVCLASS_DISPLAY, NULL, NULL, DIGCF_PRESENT);
        if (device_info_set == INVALID_HANDLE_VALUE)
            return 0;

        SP_DEVINFO_DATA devinfo_data;
        std::memset(&devinfo_data, 0, sizeof(devinfo_data));
        devinfo_data.cbSize = sizeof(devinfo_data);

        for (DWORD dev_idx = 0; SetupDiEnumDeviceInfo(device_info_set, dev_idx, &devinfo_data); dev_idx++)
        {
            const size_t buf_size = 512;
            char buf[buf_size];
            if (!SetupDiGetDeviceInstanceIdA(device_info_set, &devinfo_data, buf, buf_size, NULL))
            {
                continue;
            }

            char* vendor_pos = std::strstr(buf, "VEN_");
            if (vendor_pos != NULL && std::stoi(vendor_pos + 4, NULL, 16) == 0x8086)
            {
                char* device_pos = strstr(vendor_pos, "DEV_");
                if (device_pos != NULL)
                {
                    result = std::stoi(device_pos + 4, NULL, 16);
                    break;
                }
            }
        }

        if (device_info_set)
        {
            SetupDiDestroyDeviceInfoList(device_info_set);
        }
    }
#elif defined(__linux__)
    {
        std::string dev_base{ "/sys/class/graphics/fb" };
        int dev_idx = 0;
        for (; dev_idx < 32; dev_idx++)
        {
            std::ifstream ifs(dev_base + std::to_string(dev_idx) + "/device/vendor");
            if (ifs.good())
            {
                int ven_id;
                ifs >> std::hex >> ven_id;
                if (ven_id == 0x8086) break;
            }
        }
        if(dev_idx < 32)
        {
            std::ifstream ifs(dev_base + std::to_string(dev_idx) + "/device/device");
            if (ifs.good())
            {
                ifs >> std::hex >> result;
            }
        }
    }
#endif

    return result;
}

std::string to_string_hex(int val)
{
    auto tmp = static_cast<unsigned int>(val);
    if (tmp == 0) return "0x0";

    const char* hex_chars = "0123456789ABCDEF";

    // 64bit max
    char buf[] = "0000000000000000";
    size_t i = sizeof(buf) / sizeof(buf[0]) - 1;
    while (i > 0 && tmp > 0)
    {
        buf[--i] = hex_chars[tmp & 0xF];
        tmp >>= 4;
    }
    assert(tmp == 0);
    return std::string("0x") + &buf[i];
}

struct device_info
{
    engine_info_internal::models model;
    engine_info_internal::architectures arch;
    engine_info_internal::configurations config;
    std::string code;
};


const device_info& get_device_info(int device_id)
{
#define GEN_DEVICE(code, dev_id, model, arch, conf) { dev_id, {engine_info_internal::model, engine_info_internal::arch, engine_info_internal::conf, #code} },
    static const std::unordered_map<int, device_info> device_map{
#include "devices.inc"
    };
#undef GEN_DEVICE

    auto it = device_map.find(device_id);
    if (it == device_map.end())
        throw std::runtime_error(std::string(device_info_failed_msg) + " - unknown device id: " + to_string_hex(device_id));

    return device_map.at(device_id);
}

} // namespace <anonymous>

engine_info_internal::engine_info_internal(const gpu_toolkit& context)
{
    auto device_id = get_gpu_device_id();
    if (0 == device_id) throw std::runtime_error(device_info_failed_msg);
    auto& dev_info = get_device_info(device_id);
    model = dev_info.model;
    architecture = dev_info.arch;
    configuration = dev_info.config;

    cores_count = static_cast<uint32_t>(context.device().getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>());
    core_frequency = static_cast<uint32_t>(context.device().getInfo<CL_DEVICE_MAX_CLOCK_FREQUENCY>());

    max_work_group_size = static_cast<uint64_t>(context.device().getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>());
    max_local_mem_size = static_cast<uint64_t>(context.device().getInfo<CL_DEVICE_LOCAL_MEM_SIZE>());

    // Check for supported features.
    auto extensions = context.device().getInfo<CL_DEVICE_EXTENSIONS>();
    extensions.push_back(' '); // Add trailing space to ease searching (search with keyword with trailing space).

    supports_fp16 = extensions.find("cl_khr_fp16 ") != std::string::npos;
    supports_fp16_denorms = supports_fp16 && (context.device().getInfo<CL_DEVICE_HALF_FP_CONFIG>() & CL_FP_DENORM) != 0;
}
}}
