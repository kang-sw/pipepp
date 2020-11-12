#pragma once

namespace pipepp::impl__ {
class executor_option_base {
public:
    struct index_type {
    };

public:
    template <typename Exec_>
    void reset_as_default()
    {
        /*TODO*/
    }

private:
};

template <typename Ty_>
class executor_option_specification {
    // static variable container ~~
    // execution class 내에서 template 특수화를 통해 static option table을 initialize

private:
};

} // namespace pipepp::impl__
