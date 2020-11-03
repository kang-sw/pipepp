#pragma once
#include <iostream>
#include <functional>
#include <sstream>

namespace utility {

/**
 * Determines logger's verbosity.
 * higher value means log is more important
 */
enum class logger_verbosity {
    none,

    verbose,
    debug,
    info,
    warning,
    error,
    fatal,

    always = std::numeric_limits<int>::max()
};

#ifndef COMPILE_TIME_MUTED_VERBOSITY
/**
 * Adjust log verbosity which will be optimized during compilation
 */
#define COMPILE_TIME_MUTED_VERBOSITY none
#endif

/**
 * Default pre-logger method which decorates log message.
 * It simply 
 */
void default_pre_logger(logger_verbosity, std::ostream&);

extern logger_verbosity global_log_verbosity;

/** 
 * 로깅을 위한 클래스입니다. 런타임, 또는 컴파일 타임에 로그 활성화 여부를 선택 가능합니다.
 * ostream을 상속하는 스트림을 참조합니다.
 * 컴파일 시간에 사용되지 않는 logger가 효율적으로 최적화되도록, << 연산자를 오버로드하지 않습니다.
 */
template <
  logger_verbosity compile_time_verbosity = logger_verbosity::always,
  typename alloc_type = std::allocator<char>>
class logger
{
public:
    using allocator_type = std::allocator<char>;

public:
    static constexpr logger_verbosity static_verbosity = compile_time_verbosity;

public:
    // 매 로깅 전마다 호출되는 함수입니다.
    std::function<void(logger_verbosity, std::ostream&)> pre_logger = default_pre_logger;
    logger_verbosity dynamic_verbosity = compile_time_verbosity;

public:
    logger() noexcept {};
    logger(std::ostream* pstream) noexcept
        : strm_(pstream){};

    ~logger()
    {
        reset();
    }

    // Non-copyable
    logger(const logger&) = delete;
    logger& operator=(const logger&) = delete;

    /**
     * 단순 레퍼런스를 재지정합니다.
     * @param pstream 재지정할 레퍼런스입니다. 이 로거를 동적으로 무효화하려는 경우 널 지정
     * @note 매개 변수로 전달된 pstream 레퍼런스는 logger보다 먼저 무효화되어서는 안 됩니다
     */
    void reset(std::ostream* pstream = nullptr)
    {
        // Explicitly destroy managed object
        if (alloc_size_) {
            strm_->~basic_ostream();
            allocator_type().deallocate(reinterpret_cast<char*>(strm_), alloc_size_);
            alloc_size_ = 0;
        }

        strm_ = pstream;
    }

    /**
     * 이 로거 내부에 스트림을 생성합니다.
     * 이렇게 생성된 스트림은 관리되며, logger 파괴 시 자동으로 파괴됩니다.
     */
    template <typename stream_type,
              typename... cstr_args>
    void emplace_stream(cstr_args&&... args)
    {
        reset();
        alloc_size_ = sizeof(stream_type);
        strm_ = reinterpret_cast<std::ostream*>(allocator_type().allocate(alloc_size_));
        new (strm_) stream_type(std::forward<cstr_args>(args)...);
    }

    /**
     * 모든 인자를 출력합니다.
     */
    template <typename arg_t, typename... args_t>
    void operator()(arg_t&& arg, args_t&&... args)
    {
        if constexpr (static_verbosity > logger_verbosity::COMPILE_TIME_MUTED_VERBOSITY) {
            if (dynamic_verbosity <= global_log_verbosity)
                return;

            if (strm_ == nullptr)
                return;

            if (pre_logger)
                pre_logger(dynamic_verbosity, *strm_);

            *strm_ << std::forward<arg_t>(arg);
            ((*strm_ << std::forward<args_t>(args)), ...);
            *strm_ << '\n';
        }
    }

    /**
     * 모든 인자를 단순 출력합니다.
     * 단, pre_logger와 개행 문자가 삽입되지 않습니다.
     */
    template <typename arg_t, typename... args_t>
    void put(arg_t&& arg, args_t&&... args)
    {
        if constexpr (static_verbosity > logger_verbosity::COMPILE_TIME_MUTED_VERBOSITY) {
            if (dynamic_verbosity <= global_log_verbosity)
                return;

            if (strm_ == nullptr)
                return;

            *strm_ << std::forward<arg_t>(arg);
            ((*strm_ << std::forward<args_t>(args)), ...);
        }
    }

    template <typename Ty_>
    logger& operator<<(Ty_&& op)
    {
        if constexpr (static_verbosity > logger_verbosity::COMPILE_TIME_MUTED_VERBOSITY) {
            if (dynamic_verbosity <= global_log_verbosity)
                return *this;

            ss_ << op;
            while (!ss_.eof()) {
                auto ch = ss_.get();
                if (ch == -1 || ch == 0) { continue; }

                if (newline_) {
                    pre_log();
                    newline_ = false;
                }
                strm_->put(ch);
                newline_ = newline_ || ch == '\n';
            }
            using std::operator""s;
            ss_.str("");
            ss_.clear();
        }
        return *this;
    }

    logger& operator<<(std::ostream& (*op)(std::ostream&))
    {
        this->template operator<<<decltype(op)>(std::move(op));
        return *this;
    }

    /**
     * pre_logger를 강제 호출합니다.
     */
    void pre_log()
    {
        pre_logger(dynamic_verbosity, *strm_);
    }

private:
    std::stringstream ss_;
    std::ostream* strm_ = nullptr;
    size_t alloc_size_ = 0;
    bool newline_ = true;

public:
};

struct log {
    inline static logger<logger_verbosity::verbose> verbose{&std::cout};
    inline static logger<logger_verbosity::debug> debug{&std::cout};
    inline static logger<logger_verbosity::info> info{&std::cout};
    inline static logger<logger_verbosity::warning> warning{&std::cout};
    inline static logger<logger_verbosity::error> error{&std::cout};
    inline static logger<logger_verbosity::fatal> fatal{&std::cout};
};

} // namespace ezipc