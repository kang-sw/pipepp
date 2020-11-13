#pragma once
#include <memory>
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

    auto& option() const { return options_; }
    auto& description() const { return descriptions_; }

private:
    nlohmann::json options_;
    nlohmann::json descriptions_;
};

template <typename Exec_>
class option_specification {
    friend class option_base;

    // static variable container ~~
    // execution class 내에서 template 특수화를 통해 static option table을 initialize
public:
    nlohmann::json init_values_;
    nlohmann::json init_descs_;
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
    descriptions_ = _opt_spec<Exec_>().init_descs_;
}

template <typename Exec_, typename Ty_, size_t>
struct _option_instance {
    using spec_type = option_specification<Exec_>;

    _option_instance(Ty_&& init_value, char const* name, char const* desc)
        : name_(name)
    {
        _opt_spec<Exec_>().init_values_[name] = std::forward<Ty_>(init_value);
        _opt_spec<Exec_>().init_descs_[name] = std::string(desc);
    }

    auto& operator[](option_base& o) const { return o.options_[name_]; }
    auto& operator[](option_base const& o) const { return o.options_[name_]; }
    Ty_ operator()(option_base const& o) const { return o.options_[name_]; }

    template <typename RTy_>
    void operator()(option_base& o, RTy_&& r) const { o.options_[name_] = Ty_(std::forward<RTy_>(r)); }

    std::string const name_;
};

} // namespace impl__
} // namespace pipepp

#define PIPEPP_DEFINE_OPTION(TYPE, NAME, DEFAULT_VALUE, DESCRIPTION) \
    inline static const ::pipepp::impl__::_option_instance<          \
      ___executor_type___, TYPE, kangsw::fnv1a(#NAME)>               \
      NAME{DEFAULT_VALUE, #NAME, DESCRIPTION};

#define PIPEPP_INIT_OPTION(EXECUTOR) using ___executor_type___ = EXECUTOR;
