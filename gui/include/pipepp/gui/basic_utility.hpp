#pragma once
#include "nana/gui/programming_interface.hpp"
#include "nana/internationalization.hpp"

namespace pipepp::gui {
struct localization {
    localization() = delete;

    inline static std::string root_path = "./languages/";
    inline static std::string current_locale = "kr";

    static nana::internationalization get()
    {
        thread_local static std::string path;
        path.clear(), path.append(root_path).append(current_locale).append(".loc");
        nana::internationalization r;
        return r.load(path), std::move(r);
    }
};

extern nana::paint::font DEFAULT_DATA_FONT;

} // namespace pipepp::gui
