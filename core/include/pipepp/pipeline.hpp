#pragma once
#include <any>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <pipepp/pipe.hpp>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace pipepp {
namespace impl__ {
inline size_t hash_string(std::string_view str) { return std::hash<std::string_view>{}(str); }

class stage_base {
public:
    size_t current_fence_id_ = -1;
    std::any shared_data_ptr_;

    virtual pipe_base const *pipe() const = 0;
    virtual pipe_base *pipe() = 0;
    virtual void dispose() = 0;

    // �ܺ� �����忡�� ����Ǵ� ���������� ����
    // ����� ���� ����
    virtual void exec_loop__() = 0;

private:
    std::pair<std::condition_variable, std::mutex> event_wait_;
    std::thread stage_thread_;
    std::string stage_name_;
    std::atomic_bool pending_expire_ = false;

public:
    virtual ~stage_base() noexcept {}
};
} // namespace impl__

template <Pipe Pipe_, typename SharedData_>
class stage : public impl__::stage_base {
    using pipe_type = Pipe_;
    using input_type = typename Pipe_::input_type;
    using output_type = typename Pipe_::output_type;
    using shared_data_type = SharedData_;
    using shared_data_pointer = std::weak_ptr<shared_data_type>;
    using handler_type = std::function<void(pipe_error &, shared_data_type &, output_type const &)>;

public:
    void exec_loop__() final {
        if (pipe_.has_value() == false) {
            throw pipe_exception("Given pipe stage is not instanced");
        }

        for (;;) {
            if (!shared_data_ptr_.has_value()) {
                continue;
            }

            auto ptr = std::any_cast<shared_data_pointer &>(shared_data_ptr_).lock();
        }
    }

public:
    impl__::pipe_base const *pipe() const override { return pipe_.has_value() ? &pipe_.value() : nullptr; }
    impl__::pipe_base *pipe() override { return pipe_.has_value() ? &pipe_.value() : nullptr; }
    void dispose() override { pipe_ = {}; }

private:
    std::optional<pipe_type> pipe_;
    std::map<std::size_t, handler_type> handlers_;
    input_type reserved_input_data_;
    output_type reserved_output_data_;
    std::mutex input_data_lock_;
};

template <typename SharedData_, Pipe InitialPipe_>
class pipeline {
public:
    using shared_data_type = SharedData_;

    template <Pipe Pipe_>
    using stage_type = stage<Pipe_, shared_data_type>;
    using initial_stage_type = stage_type<InitialPipe_>;

private:
    // ������ �ڵ鷯 �ڸ��� ���ԵǴ� functor object��, ��� ���� ������ ������ ������ �� ����� ���� ���� �������� ���ȸ��� �����ϴ� ������ �����մϴ�.
    template <Pipe PipeA_, Pipe PipeB_>
    struct pipe_link {
        using output_type = typename PipeA_::output_type;
        using input_type = typename PipeB_::input_type;
        std::size_t pipe_a_hash_;
        std::size_t pipe_b_hash_;
        std::function<void(shared_data_type &, output_type const &, input_type &)> linker_;

        void operator()(pipe_error, shared_data_type &, output_type const &) {
            // v = find_pipe(pipe_b_hash_);

            // �ش� �������� �غ���� �ʾҴٸ� stall

            // �������� �غ������ linker_ ���� �� �ش� ���������� ������Ʈ ����
        }
    };

    // fence object.
    // �ϳ��� ���������� �ܰ踦 �����մϴ�.
    struct fence {
        size_t id_;
        std::shared_ptr<shared_data_type> shared_data_;
    };

public:
    decltype(auto) operator()(std::string_view view) {}

    template <Pipe PipeA_, Pipe PipeB_>
    void connect(stage_type<PipeA_> &a, stage_type<PipeB_> &b) {}

private:
    std::map<size_t, std::shared_ptr<impl__::stage_base>> stages_;
};

} // namespace pipepp