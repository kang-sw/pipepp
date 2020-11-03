#pragma once
#include <string>
#include <string_view>

namespace ezipc {

template <typename... Args_>
std::string format_string(char const* fmt, Args_&&... args)
{
    std::string s;
    auto buflen = snprintf(nullptr, 0, fmt, std::forward<Args_>(args)...);
    s.resize(buflen);

    snprintf(s.data(), buflen, fmt, std::forward<Args_>(args)...);
    return s;
}

} // namespace ezipc
