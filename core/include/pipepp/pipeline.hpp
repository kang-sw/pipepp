#pragma once
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <pipepp/pipe.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace pipepp {
namespace impl__ {
inline size_t hash_string(std::string_view str) { return std::hash<std::string_view>{}(str); }

class stage_base {
public:
    std::string stage_name_;
    size_t fence_id_;

    virtual pipe_base const *pipe() const = 0;
    virtual pipe_base *pipe() = 0;
    virtual void dispose() = 0;

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
    using handler_type = std::function<void(pipe_error &, shared_data_type &, output_type const &)>;

public:
    impl__::pipe_base const *pipe() const override { return pipe_.has_value() ? &pipe_.value() : nullptr; }
    impl__::pipe_base *pipe() override { return pipe_.has_value() ? &pipe_.value() : nullptr; }
    void dispose() override { pipe_ = {}; }

private:
    std::optional<pipe_type> pipe_;
    std::map<std::size_t, handler_type> handlers_;
};

template <typename SharedData_, Pipe InitialPipe_>
class pipeline {
public:
    using shared_data_type = SharedData_;

    template <Pipe Pipe_>
    using stage_type = stage<Pipe_, shared_data_type>;
    using initial_stage_type = stage_type<InitialPipe_>;

private:
    // 파이프 핸들러 자리에 삽입되는 functor object로, 출력 단의 파이프 실행이 끝나면 이 결과를 다음 단의 파이프에 마셜링해 전달하는 역할을 수행합니다.
    template <Pipe PipeA_, Pipe PipeB_>
    struct pipe_link {
        using output_type = typename PipeA_::output_type;
        using input_type = typename PipeB_::input_type;
        std::size_t pipe_a_hash_;
        std::size_t pipe_b_hash_;
        std::function<void(shared_data_type &, output_type const &, input_type &)> linker_;

        void operator()(pipe_error, shared_data_type &, output_type const &) {
            // v = find_pipe(pipe_b_hash_);

            // 해당 파이프가 준비되지 않았다면 stall

            // 파이프가 준비됐으면 linker_ 실행 및 해당 스테이지에 업데이트 고지
        }
    };

    // fence object.
    // 하나의 파이프라인 단계를 서술합니다.
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