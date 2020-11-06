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
 * ���������� ������ ����
 *
 * �� ���������� �������� = ������ 1�� �Ҵ�
 * ���� ��å:
 *      Synchronous Execution: ��� ������ ���������� ��� is_ready()�� ���� ���� ����
 *      Parallel Execution: ��� ����� ���������ΰ� ���ÿ� ���� ����
 *
 * ���� ��å:
 *      Primary Stage: �ݵ�� is_ready() ������ ������ ���, �ùٸ� fence index�� ����.
 *      Optional Stage: is_ready()�� ���� ���������� �õ�. �Է� assembling �Ұ�
 *                       ��, �� ��� ����� �����ϴ� ���� �ݵ�� ���� ����� �θ� �ɼų� ���������� �����ؾ� ��.
 *                       e.g.
 *                       �Ʒ��� ���, Pri0�� ����� Pri1�� ���� �Ұ�, �ݴ뵵 �Ұ�
 *                       Pri1�� Pri2�� ���� ����, �ݴ뵵 ����
 *                          Pri - Pri - Opt + Pri - Pri0
 *                                          |
 *                                          + Opt +------- Pri1
 *                                                |
 *                                                + Pri -- Pri2
 *
 * �������� ����:
 *      �Է� ó����:
 *          0) fence �����κ��� shared_data_ fetch�ؿ���
 *          1) ���� ������ �Է� ó��
 *          1.a) Primary Stage�� ���, ���� ��ũ�� ��� �Է� ó������ ���
 *          1.b) Optional Stage�� ��� �Է� ��� ���������� ���� ����
 *
 *      ������:
 *          ����� �����ͷ� ����ü�� �˰��� ����
 *
 *      ��� ó����:
 *          1) ��¿� ����� �� �������� iterate
 *          1.a) ���� ��å Synchronous�� ��� ���� stage�� �ݵ�� is_ready() ����
 *               ���������ο� ����� ���� ���������� �ϳ��� ������� ó��
 *               Pri0 + Pri1
 *                    + Pri2
 *                    + ...
 *               �� �� Pri1�� �Է� �� is_ready()���� ���, Pri2�� �Է� �� is_ready()���� ��� ... PriN���� �ݺ�.
 *               ��, �� ��� Optional ���� ��å�� �ǹ� x
 *          1.b) ���� ��å Parallel�� ���
 *          1.b.a) Primary Stage ������ is_ready()���� ���, ������ ����
 *          1.b.b) Optional Stage ������ is_ready() == false�̸� ����
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