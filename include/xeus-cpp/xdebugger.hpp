/************************************************************************************
 * Copyright (c) 2023, xeus-cpp contributors                                        *
 * Copyright (c) 2023, Johan Mabille, Loic Gouarin, Sylvain Corlay, Wolf Vollprecht *
 *                                                                                  *
 * Distributed under the terms of the BSD 3-Clause License.                         *
 *                                                                                  *
 * The full license is in the file LICENSE, distributed with this software.         *
 ************************************************************************************/


#ifndef XEUS_CPP_DEBUGGER_HPP
#define XEUS_CPP_DEBUGGER_HPP

#include <map>
#include <mutex>
#include <set>

#include "xeus_cpp_config.hpp"
#include <nlohmann/json.hpp>
#include "xeus-zmq/xdebugger_base.hpp"

namespace nl = nlohmann;

namespace xcpp
{
    class xllDB_dap_client;

    class XEUS_CPP_API debugger : public xeus::xdebugger_base 
    {
    public:
        debugger(xeus::xcontext& context, const xeus::xconfiguration& config,
            const std::string& user_name, const std::string& session_id,
            const nl::json& lldb_config);

        virtual ~debugger();

    private:
        bool start() override;
        void stop() override;
        xeus::xdebugger_info get_debugger_info() const override;
        std::string get_cell_temporary_file(const std::string& code) const override;

    private:
        xllDB_dap_client* p_lldb_dap_client;
        std::string m_lldb_host{"localhost"};
        std::string m_lldb_port{"12345"};
        nl::json m_lldb_config;
    };
    std::unique_ptr<xeus::xdebugger> make_cpp_debugger(...);
}

#endif
