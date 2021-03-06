# Pipe++ - Easy pipeline library

병렬 파이프라인 구축을 돕는 라이브러리입니다. 

# Features

``` c++
// *주의* 최종 인터페이스 아님

// 같은 파이프 스테이지의 모든 파이프가 공유하는 데이터 형식
// 파이프 객체와는 별개로, 어댑터 콜백에서 공유 데이터 형식으로부터 데이터를 추출, 
struct shared_data_type{
    ...
};

...

// 
class preprocess_pipe : public pipepp::pipe<preprocess_pipe>/*, public pipepp::json_option_interface ... 옵션 지정 가능한 클래스라면 */
{
public: // Required template interfaces 
    struct input_type {
        ...
    };
    struct result_type {
        ...
    };
    
    // pipe::supply(Fn<void(input_type&)>) 가 호출될 때마다 호출
    // --> supply 함수는 내부적으로 lock 걸고 함수 콜백으로 내부 파라미터에 값 복사 ...
    pipepp::error_t exec_once(input_type const& i, result_type& o)
    {
        // pipe_base::elapse_timer 구조체
        auto elapse_scope_total = elapse_scope("Total Scope"pipehash);
        
        if(elapse_scope_total.check_dbg_flag("Debug Elem 1"pipehash)){
            // Do some debugging operation ...
            elapse_scope_total.set_dbg_argument(some_arg); // std::any
        }
        
        {
            auto elapse_scope_block1 = elapse_scope("Block1 Scope"pipehash);
        }
    }
};

class some_other_pipe_1 : public pipepp::pipe<preprocess_pipe>;
class some_other_pipe_2 : public pipepp::pipe<preprocess_pipe>;

class parallel_pipe_a : public pipepp::pipe<>
...
    pipepp::pipeline_factory<shared_data_type, preprocess_pipe> pf {.../* preprocess_pipe 파라미터 */};
    // pipepp::pipeline_factory<shared_data_type>::proxy_type<pipe_type>
    auto pp_pipe_proxy = pf.first_pipe(); // implicitly preprocess_pipe. 
    // 하나의 파이프를 생성합니다.
    auto p1_pipe_proxy = pf.emplace_pipe<some_other_pipe_1>("pipe 1 id", ...); 
    // 병렬로 실행되는 다수의 파이프(파이프 그룹)를 생성합니다.
    // 파이프 그룹은 준비된 인스턴스부터 값을 받아, 오래 걸리는 파이프의 처리 시간을 분산합니다.
    // 처리 시간이 80ms이고, 파이프 그룹의 인스턴스 개수가 4개라면 20ms 속도의 파이프와 stall 없이 연결 가능
    auto p2_pipe_proxy = pf.emplace_pipe_parallel<some_other_pipe_2>("pipe group 2 id", n, ...);
    
    // 파이프 에러 핸들링 함수
    // 미설정시 terminate
    pp_pipe_proxy.error_handler([](shared_data_type& d, std::exception& e){});
    
    // 출력 핸들러 함수 
    pp_pipe_proxy.add_output_handler([](shared_data_type& shared_data, preprocess_pipe::result_type const& result) {});
    
    // 어댑터 함수 ... 파이프 실행 성공시 자동으로 연결된 파이프의 supply_partial() 함수 호출, 공급.
    // 이 때, 다수의 파이프가 하나의 파이프에 add_pipe_link 호출시 연결된 파이프 모두 supply하기 전까지 블록...
    pp_pipe_proxy.add_pipe_link(p1_pipe_proxy, [](shared_data_type& shared_data, preprocess_pipe::result_type const& in_result, some_other_pipe_1::input_type& out_input) {});
    // add_async_pipe_link: 파이프를 비동기적으로 연결합니다. 새로운 스레드를 생성해, 이벤트 기반 처리 수행
    
    // 기본적으로 모든 파이프 연결은 인자 전달 순서(handler invoke order)가 out-of-order입니다.
    pp_pipe_proxy.add_pipe_link(p2_pipe_proxy, ...);
    
    // 파이프라인 팩토리 인스턴시에이트.
    // 이후 pf는 invalid state가 됨.
    // 명시적으로 파이프의 런타임 구조 변경을 금지시키기 위함!
    // pipepp::pipeline<first_pipe_type:preprocess_pipe>
    auto pipeline = pf.create();
...

...
    // 루프
    while(is_running){
        while(!pipeline.first_pipe_ready()){yield();}
        pipeline.supply_first(val:/*preprocess_pipe::input_type*/);
    }
    
    pipeline.flush(); // Synchronous block operation. 호출 안하면 자동 호출
    pipeline.destroy(); // Destroy explicitly. 호출 안하면 소멸자에서 자동 호출
...

```

# References

- [nlohmann/json](https://github.com/nlohmann/json) 
- [cnjinhao/nana](https://github.com/cnjinhao/nana)