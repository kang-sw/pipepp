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
/**
 * 파이프라인 디자인 개요
 *
 * 각 파이프라인 스테이지 = 스레드 1개 할당
 * 실행 정책:
 *      Synchronous Execution: 출력 연결한 파이프라인 모두 is_ready()일 때만 실행 가능
 *      Parallel Execution: 출력 연결된 파이프라인과 동시에 실행 가능
 *
 * 시작 정책:
 *      Primary Stage: 반드시 is_ready() 상태일 때까지 대기, 올바른 fence index를 전달.
 *      Optional Stage: is_ready()일 때만 파이프라인 시동. 입력 assembling 불가
 *                       단, 이 경우 출력을 연결하는 노드는 반드시 가장 가까운 부모 옵셔널 스테이지를 공유해야 함.
 *                       e.g.
 *                       아래의 경우, Pri0의 출력을 Pri1에 연결 불가, 반대도 불가
 *                       Pri1은 Pri2에 연결 가능, 반대도 가능
 *                          Pri - Pri - Opt + Pri - Pri0
 *                                          |
 *                                          + Opt +------- Pri1
 *                                                |
 *                                                + Pri -- Pri2
 *
 * 스테이지 구조:
 *      입력 처리기:
 *          0) fence 값으로부터 shared_data_ fetch해오기
 *          1) 이전 파이프 입력 처리
 *          1.a) Primary Stage의 경우, 이전 링크의 모든 입력 처리까지 대기
 *          1.b) Optional Stage의 경우 입력 즉시 파이프라인 가동 시작
 *
 *      파이프:
 *          입출력 데이터로 구현체의 알고리즘 실행
 *
 *      출력 처리기:
 *          1) 출력에 연결된 각 스테이지 iterate
 *          1.a) 실행 정책 Synchronous인 경우 다음 stage는 반드시 is_ready() 상태
 *               파이프라인에 연결된 다음 파이프라인 하나씩 순서대로 처리
 *               Pri0 + Pri1
 *                    + Pri2
 *                    + ...
 *               일 때 Pri1에 입력 후 is_ready()까지 대기, Pri2에 입력 후 is_ready()까지 대기 ... PriN까지 반복.
 *               즉, 이 경우 Optional 시작 정책은 의미 x
 *          1.b) 실행 정책 Parallel인 경우
 *          1.b.a) Primary Stage 연결은 is_ready()까지 대기, 데이터 전달
 *          1.b.b) Optional Stage 연결은 is_ready() == false이면 생략
 *
 */
namespace impl__ {
template <typename SharedData_>
class stage_base : public std::enable_shared_from_this<stage_base<SharedData_>> {
    template <SharedData_>
    friend class pipeline_base;

public:
    using shared_data_type = SharedData_;

public:
    void dispose();

private:
    void loop__() {
        while (pending_dispose_ == false) {
            // fetch next pipe data

            // wait until all inputs are gathered
            // if current fence is invalidated, do 'continue'

            // run pipe + handler
        }
        is_disposed_ = true;
    }

protected:
    void submit_input__();

private:
    virtual void exec_pipe__() = 0;

private:
    pipeline_base<shared_data_type>* pipeline_;
    std::string name_;
    std::atomic_bool is_disposed_ = false;
    std::atomic_bool pending_dispose_ = false;
    std::thread owning_thread_;
    std::pair<std::condition_variable, std::mutex> event_wait_;

    struct {
        std::weak_ptr<shared_data_type> fetched_shared_data_;
        size_t fence_index = 0;
        size_t num_gathered_inputs = 0;
    } input_desc_;
};

} // namespace impl__

template <Pipe Pipe_, typename SharedData_>
class stage : public impl__::stage_base<SharedData_> {};
} // namespace pipepp