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
 * �α��� ���� Ŭ�����Դϴ�. ��Ÿ��, �Ǵ� ������ Ÿ�ӿ� �α� Ȱ��ȭ ���θ� ���� �����մϴ�.
 * ostream�� ����ϴ� ��Ʈ���� �����մϴ�.
 * ������ �ð��� ������ �ʴ� logger�� ȿ�������� ����ȭ�ǵ���, << �����ڸ� �����ε����� �ʽ��ϴ�.
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
    // �� �α� ������ ȣ��Ǵ� �Լ��Դϴ�.
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
     * �ܼ� ���۷����� �������մϴ�.
     * @param pstream �������� ���۷����Դϴ�. �� �ΰŸ� �������� ��ȿȭ�Ϸ��� ��� �� ����
     * @note �Ű� ������ ���޵� pstream ���۷����� logger���� ���� ��ȿȭ�Ǿ�� �� �˴ϴ�
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
     * �� �ΰ� ���ο� ��Ʈ���� �����մϴ�.
     * �̷��� ������ ��Ʈ���� �����Ǹ�, logger �ı� �� �ڵ����� �ı��˴ϴ�.
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
     * ��� ���ڸ� ����մϴ�.
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
     * ��� ���ڸ� �ܼ� ����մϴ�.
     * ��, pre_logger�� ���� ���ڰ� ���Ե��� �ʽ��ϴ�.
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
     * pre_logger�� ���� ȣ���մϴ�.
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