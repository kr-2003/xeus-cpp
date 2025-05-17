/************************************************************************************
 * Copyright (c) 2023, xeus-cpp contributors                                        *
 * Copyright (c) 2023, Johan Mabille, Loic Gouarin, Sylvain Corlay, Wolf Vollprecht *
 *                                                                                  *
 * Distributed under the terms of the BSD 3-Clause License.                         *
 *                                                                                  *
 * The full license is in the file LICENSE, distributed with this software.         *
 ************************************************************************************/


#include "xeus-cpp/xdebugger.hpp"
#include "xeus-zmq/xmiddleware.hpp"
#include "xeus/xinterpreter.hpp"
#include "xeus/xsystem.hpp"
#include "xeus-zmq/xmiddleware.hpp"

namespace xcpp 
{
    debugger::debugger(xeus::xcontext& context, const xeus::xconfiguration& config,
                       const std::string& user_name, const std::string& session_id,
                       const nl::json& lldb_config)
        : xdebugger_base(context)
        , p_lldb_dap_client(nullptr)
        , m_lldb_host("localhost")
        , m_lldb_port("12345")
        , m_lldb_config(lldb_config)
    {
    }

    debugger::~debugger()
    {
        // delete p_lldb_dap_client;
        // p_lldb_dap_client = nullptr;
    }

    bool debugger::start()
    {
        return true;
    }

    void debugger::stop()
    {
        return;
    }

    xeus::xdebugger_info debugger::get_debugger_info() const
    {
        // return temporary values
        return xeus::xdebugger_info(1,
                        "temp_prefix",
                        "temp_suffix",
                        true,
                        {"Temporary Exceptions"},
                        true);
    }

    std::string debugger::get_cell_temporary_file(const std::string& code) const
    {
        return "";
    }

    std::unique_ptr<xeus::xdebugger> make_cpp_debugger(xeus::xcontext& context,
                                                       const xeus::xconfiguration& config,
                                                       const std::string& user_name,
                                                       const std::string& session_id,
                                                       const nl::json& lldb_config)
    {
        return std::unique_ptr<xeus::xdebugger>(new debugger(context, config, user_name, session_id, lldb_config));
    }
}