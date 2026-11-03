#pragma once
#include <chrono>
#include <cstdlib>
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif
namespace one::vulkan {
inline double clockMs() {
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline double waitThreadCpuMs() {
    static const bool enabled=std::getenv("THEONE_PROFILE_WAITS")!=nullptr;
    if(!enabled)return 0;
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    if(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)) {
        auto value=[](FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
        return double(value(kernel)+value(user))/10000.0;
    }
#endif
    return 0;
}
}
