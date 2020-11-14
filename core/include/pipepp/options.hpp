#pragma once
#include <memory>
#include <shared_mutex>
#include <span>
#include <variant>
#include "kangsw/misc.hxx"
#include "nlohmann/json.hpp"

namespace pipepp {
namespace impl__ {
template <typename Exec_, typename Ty_, size_t>
struct _option_instance;

class option_base final {
    template <typename Exec_, typename Ty_, size_t>
    friend struct _option_instance;

public:
    using variant_type = std::variant<int64_t, double, std::string>;

public:
    template <typename Exec_>
    void reset_as_default();

    auto lock_read(bool trial = false) const
    {
        return trial ? std::shared_lock{lock_, std::try_to_lock} : std::shared_lock{lock_};
    }
    auto lock_write(bool trial = false) const
    {
        return trial ? std::unique_lock{lock_, std::try_to_lock} : std::unique_lock{lock_};
    }

    auto& option() const { return options_; }
    auto& option() { return options_; }
    auto& description() const { return descriptions_; }
    auto& categories() const { return categories_; }

private:
    nlohmann::json options_;
    nlohmann::json descriptions_;
    std::map<std::string, std::string> categories_;
    mutable std::shared_mutex lock_;
};

template <typename Exec_>
class option_specification {
    friend class option_base;

public:
    nlohmann::json init_values_;
    nlohmann::json init_descs_;
    std::map<std::string, std::string> init_categories_;
};

template <typename Exec_>
option_specification<Exec_>& _opt_spec()
{
    static option_specification<Exec_> spec;
    return spec;
};

template <typename Exec_>
void option_base::reset_as_default()
{
    options_ = _opt_spec<Exec_>().init_values_;
    categories_ = _opt_spec<Exec_>().init_categories_;
    descriptions_ = _opt_spec<Exec_>().init_descs_;
}

template <typename Exec_, typename Ty_, size_t>
struct _option_instance {
    using spec_type = option_specification<Exec_>;

    _option_instance(Ty_&& init_value, char const* name, char const* category = "", char const* desc = "")
        : name_(name)
    {
        _opt_spec<Exec_>().init_values_[name] = std::forward<Ty_>(init_value);
        _opt_spec<Exec_>().init_categories_[name] = std::string(category);
        _opt_spec<Exec_>().init_descs_[name] = std::string(desc);
    }

    template <typename RTy_>
    void operator()(option_base& o, RTy_&& r) const { o.lock_write(), o.options_[name_] = Ty_(std::forward<RTy_>(r)); }
    Ty_ operator()(option_base const& o) const { return o.lock_read(), o.options_[name_].get<Ty_>(); }

    std::string const name_;
};

} // namespace impl__
} // namespace pipepp

/**
 * PIPEPP_DEFINE_OPTION(TYPE, NAME, DEFAULT_VALUE [, CATEGORY[, DESCRIPTION]])
 */
#define PIPEPP_DEFINE_OPTION(TYPE, NAME, DEFAULT_VALUE, ...) \
    inline static const ::pipepp::impl__::_option_instance<  \
      ___executor_type___, TYPE, kangsw::fnv1a(#NAME)>       \
      NAME{DEFAULT_VALUE, #NAME, ##__VA_ARGS__};

#define PIPEPP_DEFINE_OPTION_CLASS(EXECUTOR) using ___executor_type___ = EXECUTOR;
