#pragma once
#include <any>
#include <array>
#include <chrono>
#include <shared_mutex>
#include <string>
#include <variant>

#include "kangsw/hash_index.hxx"
#include "kangsw/misc.hxx"
#include "kangsw/spinlock.hxx"

namespace pipepp {
namespace impl__ {
class option_base;
} // namespace impl__

/**
 * ���� string table�� ���ɴϴ�. ��� execution_context�� �� �Լ��� ���.
 */
kangsw::safe_string_table& string_pool();

/**
 * ���� ���� ������ ����
 *
 * ������ Ÿ�̸� ����
 * ������ ������ ����
 *
 * �� �����ϰ� �ֽ��ϴ�.
 */
struct execution_context_data {
    using clock = std::chrono::system_clock;
    using debug_variant = std::variant<bool, int64_t, double, std::string, std::any>;
    friend class execution_context;

public:
    struct timer_entity {
        std::string_view name;
        size_t category_level;
        clock::duration elapsed;
    };

    struct debug_data_entity {
        std::string_view name;
        size_t category_level;
        debug_variant data;
    };

public:
    std::vector<timer_entity> timers;
    std::vector<debug_data_entity> debug_data;
};

/**
 * ���� ���� Ŭ����.
 * ����� �� ����͸��� ���� Ŭ������,
 *
 * 1) ����� �÷��� ����
 * 2) ����� ������ ����
 * 3) ������ ������ ���� �ð� ����
 * 4) �ɼǿ� ���� ���� ����(�ɼ� �ν��Ͻ��� pipe�� ����, ���� ������ ���۷��� �б� ����)
 *
 * ���� ����� �����մϴ�.
 *
 * ���ο� �� ���� ������ ���۸� ���� ������, �⺻������ clear_records�� ȣ��� ������ �� ���� ���۸� �����մϴ�.
 * �̸� ���� �޸� ���Ҵ��� ������ �� �ִµ�, ���� �ܺο��� context�� ��û�� ��� �� ������ ������Ʈ�� �ѱ�� ������մϴ�.
 */
class execution_context {
public:
    template <typename Ty_> using lock_type = std::unique_lock<Ty_>;
    using clock = std::chrono::system_clock;

    // TODO: ����� �÷��� ����
    // TODO: ����� ������ ����(variant<bool, long, double, string, any> [])
    // TODO: ���� �ð� ������

public:
    struct timer_scope_indicator {
        ~timer_scope_indicator();
        timer_scope_indicator() = default;
        timer_scope_indicator(timer_scope_indicator&&) = default;
        timer_scope_indicator& operator=(timer_scope_indicator&&) = default;

    private:
        friend class execution_context;
        execution_context* self_;
        size_t index_ = {};
        clock::time_point issue_;
        kangsw::ownership owning_;
    };

public:
    execution_context();

public: // accessor
    auto const& option() const { return *options_; }
    operator impl__::option_base const &() const { return *options_; }

public: // methods
    /**
     * @brief �Է��� ���� ������ �������� Ȯ���մϴ�.
     * @return ���� �Է��� ���� �����ϸ� true
     */
    bool can_consume_read_buffer() const { return rd_buf_valid_.test(std::memory_order_relaxed); }

    /**
     * ���� ������ ��ȿ�� Ÿ�̸Ӹ� �����մϴ�.
     * ī�װ��� �ϳ� ������ŵ�ϴ�. 
     */
    timer_scope_indicator timer_scope(kangsw::hash_pack name);

    /**
     * ����� ������ ���Ӱ� �����մϴ�.
     */
    template <typename Ty_>
    void store_debug_data(kangsw::hash_pack, Ty_&& value);

public:                    // internal public methods
    void _clear_records(); // invoke() ���� ȣ��
    void _internal__set_option(impl__::option_base const* opt) { options_ = opt; }
    void _swap_data_buff(); // invoke() ���� ȣ��

    /**
     * �б� ���۸� �����մϴ�.
     * ���� _clear_records() ȣ�� ������ �����͸� ������ �� ���� ���°� �˴ϴ�.
     */
    std::shared_ptr<execution_context_data> _consume_read_buffer();

private: // private methods
    auto& _rd() { return context_data_[!front_data_buffer_]; }
    auto& _wr() { return context_data_[front_data_buffer_]; }

private:
    class impl__::option_base const* options_ = {};

    std::array<std::shared_ptr<execution_context_data>, 2> context_data_;
    bool front_data_buffer_ = false;
    kangsw::spinlock swap_lock_;
    std::atomic_flag rd_buf_valid_;

    size_t category_level_ = 0;
};

template <typename Ty_>
void execution_context::store_debug_data(kangsw::hash_pack hp, Ty_&& value)
{
    auto& entity = _wr()->debug_data.emplace_back();
    entity.category_level = category_level_;
    entity.name = string_pool()(hp).second;
    auto& data = entity.data;

    if constexpr (std::is_same_v<bool, Ty_>) {
        data = value;
    }
    else if constexpr (std::is_integral_v<Ty_>) {
        data.emplace<int64_t>(std::forward<Ty_>(value));
    }
    else if constexpr (std::is_floating_point_v<Ty_>) {
        data.emplace<double>(std::forward<Ty_>(value));
    }
    else if constexpr (std::is_convertible_v<Ty_, std::string>) {
        data.emplace<std::string>(std::forward<Ty_>(value));
    }
    else {
        data.emplace<std::any>(std::forward<Ty_>(value));
    }
}
} // namespace pipepp