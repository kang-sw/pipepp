#pragma once
#include <memory>
#include <set>
#include <shared_mutex>
#include <span>
#include <unordered_set>
#include <variant>
#include "kangsw/misc.hxx"
#include "nlohmann/json.hpp"

namespace pipepp {
namespace detail {
template <typename Exec_, typename Ty_>
struct _option_instance;
using verify_function_t = std::function<bool(nlohmann::json&)>;

class option_base final {
    template <typename Exec_, typename Ty_>
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

    auto& value() const { return options_; }
    auto& value() { return options_; }
    auto& description() const { return descriptions_; }
    auto& categories() const { return categories_; }
    auto& names() const { return names_; }
    auto& paths() const { return paths_; }

    bool verify(std::string const& n, nlohmann::json& arg) const
    {
        return verifiers_.at(n)(arg);
    }

    bool verify(std::string const& n)
    {
        auto& json = value().at(n);
        auto& verify = verifiers_.at(n);
        return verify(json);
    }

private:
    nlohmann::json options_;
    std::map<std::string, std::string> descriptions_;
    std::map<std::string, std::string> categories_;
    std::map<std::string, std::string> names_;
    std::map<std::string, verify_function_t> verifiers_;
    std::map<std::string, std::string> paths_;
    mutable std::shared_mutex lock_;
};

template <typename Exec_>
class option_specification {
    friend class option_base;

public:
    nlohmann::json init_values_;
    std::map<std::string, std::string> init_descs_;
    std::map<std::string, std::string> init_categories_;
    std::map<std::string, std::string> init_names_;
    std::map<std::string, verify_function_t> init_verifies_;
    std::map<std::string, std::string> paths_;
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
    names_ = _opt_spec<Exec_>().init_names_;
    verifiers_ = _opt_spec<Exec_>().init_verifies_;
    paths_ = _opt_spec<Exec_>().paths_;
}

template <typename Exec_, typename Ty_>
struct _option_instance {
    using spec_type = option_specification<Exec_>;
    using value_type = Ty_;

    _option_instance(
      std::string path,
      Ty_&& init_value,
      std::string name,
      std::string category = "",
      std::string desc = "",
      std::function<bool(Ty_&)> verifier = [](auto&) { return true; })
        : key_(category + "." + name)
    {
        if (_opt_spec<Exec_>().init_values_.contains(key_)) throw;

        auto initv = std::forward<Ty_>(init_value);
        verifier(initv);

        verify_function_t verify = [fn = std::move(verifier)](nlohmann::json& arg) -> bool {
            Ty_ value = arg;

            if (!fn(value)) { return arg = value, false; }
            return true;
        };

        _opt_spec<Exec_>().paths_[key_] = path;
        _opt_spec<Exec_>().init_values_[key_] = std::move(initv);
        _opt_spec<Exec_>().init_categories_[key_] = std::move(category);
        _opt_spec<Exec_>().init_descs_[key_] = std::move(desc);
        _opt_spec<Exec_>().init_names_[key_] = std::move(name);
        _opt_spec<Exec_>().init_verifies_[key_] = std::move(verify);
    }

    template <typename RTy_>
    void operator()(option_base& o, RTy_&& r) const { o.lock_write(), o.options_[key_] = Ty_(std::forward<RTy_>(r)); }
    Ty_ operator()(option_base const& o) const
    {
        Ty_ value;
        o.lock_read(), value = o.options_[key_].get<Ty_>();
        return value;
    }

    std::string const key_;
};

} // namespace detail

namespace verify {

template <typename Ty_>
struct _verify_chain {
    template <typename Fn_>
    friend _verify_chain operator|(_verify_chain A, Fn_&& B)
    {
        A.fn_ = [fn_a = std::move(A.fn_), fn_b = std::forward<Fn_>(B)](Ty_& r) -> bool { return fn_a(r) && fn_b(r); };
        return std::move(A);
    }

    template <typename Fn_>
    _verify_chain(Fn_&& fn)
        : fn_(std::forward<Fn_>(fn))
    {}

    bool operator()(Ty_& r) const { return fn_(r); }
    std::function<bool(Ty_&)> fn_;
};

template <typename Ty_>
_verify_chain<Ty_> minimum(Ty_&& min)
{
    return [min_ = std::forward<Ty_>(min)](Ty_& r) {
        if (r < min_) { return r = min_, false; }
        return true;
    };
}

template <typename Ty_>
_verify_chain<Ty_> maximum(Ty_&& max)
{
    return [max_ = std::forward<Ty_>(max)](Ty_& r) {
        if (r > max_) { return r = max_, false; }
        return true;
    };
}

template <typename Ty_>
auto clamp(Ty_&& min, Ty_&& max)
{
    return minimum(std::forward<Ty_>(min)) | maximum(std::forward<Ty_>(max));
}

template <typename Ty_, typename... Args_>
_verify_chain<Ty_> contains(Ty_&& first, Args_&&... args)
{
    return [set_ = std::unordered_set<Ty_>({std::forward<Ty_>(first), std::forward<Args_>(args)...})](Ty_& r) {
        if (!set_.contains(r)) return r = *set_.begin(), false;
        return true;
    };
}

template <typename Ty_, typename RTy_>
_verify_chain<Ty_> minimum_all(RTy_&& pivot)
{
    return [pvt_ = std::forward<RTy_>(pivot)](Ty_& r) {
        bool modified_any = false;
        for (auto& val : r) {
            if (val < pvt_) { val = pvt_, modified_any = true; }
        }
        return !modified_any;
    };
}

template <typename Ty_, typename RTy_>
_verify_chain<Ty_> maximum_all(RTy_&& pivot)
{
    return [pvt_ = std::forward<RTy_>(pivot)](Ty_& r) {
        bool modified_any = false;
        for (auto& val : r) {
            if (val > pvt_) { val = pvt_, modified_any = true; }
        }
        return !modified_any;
    };
}

template <typename Ty_, typename RTy_>
_verify_chain<Ty_> clamp_all(RTy_&& min, RTy_&& max)
{
    return minimum_all<Ty_>(std::forward<RTy_>(min)) | maximum_all<Ty_>(std::forward<RTy_>(max));
}

template <typename Ty_, typename RTy_, typename... Args_>
_verify_chain<Ty_> contains_all(RTy_&& first, Args_&&... args)
{
    return [set_ = std::unordered_set<RTy_>({std::forward<RTy_>(first), std::forward<Args_>(args)...})](Ty_& r) {
        bool modify_any = false;
        for (auto& val : r) {
            if (!set_.contains(val)) val = *set_.begin(), modify_any = true;
        }
        return !modify_any;
    };
}

template <typename Ty_>
_verify_chain<Ty_> ascending()
{
    return [](Ty_& r) {
        if (!std::is_sorted(std::begin(r), std::end(r), std::less_equal<>{})) {
            std::sort(std::begin(r), std::end(r), std::less_equal<>{});
            return false;
        }
        return true;
    };
}

template <typename Ty_>
_verify_chain<Ty_> descending()
{
    return [](Ty_& r) {
        if (!std::is_sorted(std::begin(r), std::end(r), std::greater_equal<>{})) {
            std::sort(std::begin(r), std::end(r), std::greater_equal<>{});
            return false;
        }
        return true;
    };
}

} // namespace verify

namespace detail {
static std::string path_tostr(const char* path, int line)
{
    auto out = std::string(path);
    if (auto sz = out.find_last_of("\\/"); sz != std::string::npos) {
        out = out.substr(sz + 1);
    }
    out.append(" (").append(std::to_string(line)).append(")");
    return std::move(out);
}
} // namespace detail
} // namespace pipepp
