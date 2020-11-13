#pragma once
#include <any>
#include <array>
#include <map>
#include <shared_mutex>
#include <string>
#include <variant>
#include <winbase.h>

#include "kangsw/hash_index.hxx"
#include "kangsw/spinlock.hxx"

namespace pipepp {
namespace impl__ {
class option_base;
} // namespace impl__

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
    using debug_variant = std::variant<bool, long, double, std::string, std::any>;
    friend class execution_context;

public:
    struct timer_entity {
        char const* name;
        size_t category_level;
        clock::duration elapsed;
    };

    struct debug_flag_entity {
        kangsw::hash_index index;
        size_t category_level;
        size_t order;
    };

    struct debug_data_entity {
        char const* name;
        size_t category_level;
        debug_variant data;
    };

public:
    std::vector<timer_entity> timers;
    std::map<kangsw::hash_index, debug_flag_entity> flags;
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

    // TODO: ����� �÷��� ����
    // TODO: ����� ������ ����(variant<bool, long, double, string, any> [])
    // TODO: ���� �ð� ������

public:
    execution_context()
    {
        for (auto& pt : context_data_) { pt = std::make_shared<execution_context_data>(); }
    }

public: // accessor
    auto const& option() const { return *options_; }
    operator impl__::option_base const &() const { return *options_; }

public: // methods
    /**
     * �б� ���۸� �����մϴ�.
     * ���� _clear_records() ȣ�� ������ �����͸� ������ �� ���� ���°� �˴ϴ�.
     */
    std::shared_ptr<execution_context_data> consume_read_buffer();

public:                    // internal public methods
    void _clear_records(); // invoke() ���� ȣ��
    void _internal__set_option(impl__::option_base const* opt) { options_ = opt; }
    void _swap_data_buff(); // invoke() ���� ȣ��

private: // private methods
    auto& _rd() { return context_data_[!front_data_buffer_]; }
    auto& _wr() { return context_data_[front_data_buffer_]; }

private:
    class impl__::option_base const* options_ = {};
    std::map<kangsw::hash_index, execution_context_data::debug_flag_entity> flags;
    std::array<std::shared_ptr<execution_context_data>, 2> context_data_;
    bool front_data_buffer_ = false;
    kangsw::spinlock swap_lock_;
    std::atomic_flag rd_buf_valid;
};

} // namespace pipepp