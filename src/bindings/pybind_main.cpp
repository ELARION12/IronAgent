#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "Agent.hpp"
#include "AgentState.hpp"
#include "ToolRegistry.hpp"
#include "ContextBuffer.hpp"

namespace py = pybind11;
using namespace coreagent;

/* ─────────────────────────────────────────────────────────
 * CoreAgent Python Bindings
 * Exposes C++ engine to Python via pybind11
 * ───────────────────────────────────────────────────────── */

PYBIND11_MODULE(_coreagent_cpp, m) {
    /* ── AgentState enum ──────────────────────────────── */
    py::enum_<AgentState>(m, "AgentState")
        .value("IDLE",       AgentState::IDLE)
        .value("THINKING",   AgentState::THINKING)
        .value("PLANNING",   AgentState::PLANNING)
        .value("ACTING",     AgentState::ACTING)
        .value("REFLECTING", AgentState::REFLECTING)
        .value("DONE",       AgentState::DONE)
        .value("ERROR",      AgentState::ERROR)
        .export_values();

    /* ── MessageRole enum ─────────────────────────────── */
    py::enum_<MessageRole>(m, "MessageRole")
        .value("SYSTEM",    MessageRole::SYSTEM)
        .value("USER",      MessageRole::USER)
        .value("ASSISTANT", MessageRole::ASSISTANT)
        .value("TOOL",      MessageRole::TOOL)
        .export_values();

    /* ── ToolInput ────────────────────────────────────── */
    py::class_<ToolInput>(m, "ToolInput")
        .def(py::init<>())
        .def_readwrite("name", &ToolInput::name)
        .def_readwrite("args", &ToolInput::args)
        .def("__repr__", [](const ToolInput& t) {
            return "<ToolInput name='" + t.name
                 + "' args='" + t.args + "'>";
        });

    /* ── ToolOutput ───────────────────────────────────── */
    py::class_<ToolOutput>(m, "ToolOutput")
        .def(py::init<>())
        .def_readwrite("result",     &ToolOutput::result)
        .def_readwrite("success",    &ToolOutput::success)
        .def_readwrite("error",      &ToolOutput::error)
        .def_readwrite("elapsed_ms", &ToolOutput::elapsed_ms)
        .def("__repr__", [](const ToolOutput& o) {
            return "<ToolOutput success=" +
                   std::string(o.success ? "True" : "False") +
                   " result='" + o.result.substr(0, 40) + "'>";
        });

    /* ── ToolRegistry ─────────────────────────────────── */
    py::class_<ToolRegistry>(m, "ToolRegistry")
        .def(py::init<>())
        .def("register_tool",
            [](ToolRegistry& reg,
               const std::string& name,
               const std::string& desc,
               py::function py_fn) {
                reg.registerTool(name, desc,
                    [py_fn](const ToolInput& input) -> ToolOutput {
                        py::gil_scoped_acquire acquire;
                        try {
                            py::object result = py_fn(input);
                            return result.cast<ToolOutput>();
                        } catch (const py::error_already_set& e) {
                            return ToolOutput{"", false,
                                std::string("Python error: ") + e.what(), 0};
                        }
                    });
            },
            py::arg("name"),
            py::arg("description"),
            py::arg("fn")
        )
        .def("dispatch",    &ToolRegistry::dispatch)
        .def("has_tool",    &ToolRegistry::hasTool)
        .def("tool_names",  &ToolRegistry::toolNames)
        .def("size",        &ToolRegistry::size)
        .def("print_all",   &ToolRegistry::printAll);

    /* ── Message ──────────────────────────────────────── */
    py::class_<Message>(m, "Message")
        .def(py::init<>())
        .def_readwrite("role",         &Message::role)
        .def_readwrite("content",      &Message::content)
        .def_readwrite("tool_name",    &Message::tool_name)
        .def_readwrite("timestamp_ms", &Message::timestamp_ms);

    /* ── ContextBuffer ────────────────────────────────── */
    py::class_<ContextBuffer>(m, "ContextBuffer")
        .def(py::init<size_t, size_t, size_t>(),
             py::arg("arena_capacity"),
             py::arg("scratch_capacity"),
             py::arg("max_tokens") = 4096)
        .def("add_system",      &ContextBuffer::addSystem)
        .def("add_user",        &ContextBuffer::addUser)
        .def("add_assistant",   &ContextBuffer::addAssistant)
        .def("add_tool_result", &ContextBuffer::addToolResult)
        /* ── ZERO-COPY BUFFER PROTOCOL START ── */
        .def("build_prompt", [](ContextBuffer& cb) -> py::memoryview {
            std::string_view sv = cb.buildPrompt();
            
            // Expose the C arena memory directly to Python without copying
            return py::memoryview::from_memory(
                const_cast<char*>(sv.data()), 
                sv.size(), 
                true // readonly flag prevents Python from corrupting the C arena
            );
        })
        /* ── ZERO-COPY BUFFER PROTOCOL END ── */
        .def("messages",        &ContextBuffer::messages)
        .def("message_count",   &ContextBuffer::messageCount)
        .def("token_count",     &ContextBuffer::tokenCount)
        .def("retained_bytes_used", &ContextBuffer::retainedBytesUsed)
        .def("clear",           &ContextBuffer::clear)
        .def("print_summary",   &ContextBuffer::printSummary);

    /* ── AgentConfig ──────────────────────────────────── */
    py::class_<AgentConfig>(m, "AgentConfig")
        .def(py::init<>())
        .def_readwrite("name",          &AgentConfig::name)
        .def_readwrite("system_prompt", &AgentConfig::system_prompt)
        .def_readwrite("max_tokens",    &AgentConfig::max_tokens)
        .def_readwrite("num_threads",   &AgentConfig::num_threads)
        .def_readwrite("max_steps",     &AgentConfig::max_steps)
        .def_readwrite("tool_timeout",  &AgentConfig::tool_timeout)
        .def_readwrite("context_arena_capacity",   &AgentConfig::context_arena_capacity)
        .def_readwrite("context_scratch_capacity", &AgentConfig::context_scratch_capacity);

    /* ── Agent ────────────────────────────────────────── */
    py::class_<Agent>(m, "Agent")
        .def(py::init<const AgentConfig&>(),
             py::arg("config") = AgentConfig{})

        .def("run",   &Agent::run,
             py::arg("task"),
             py::call_guard<py::gil_scoped_release>())

        .def("register_tool",
            [](Agent& agent,
               const std::string& name,
               const std::string& desc,
               py::function py_fn) {
                agent.registerTool(name, desc,
                    [py_fn](const ToolInput& input) -> ToolOutput {
                        py::gil_scoped_acquire acquire;
                        try {
                            py::object result = py_fn(input);
                            return result.cast<ToolOutput>();
                        } catch (const py::error_already_set& e) {
                            return ToolOutput{"", false,
                                std::string("Python error: ") + e.what(), 0};
                        }
                    });
            },
            py::arg("name"),
            py::arg("description"),
            py::arg("fn")
        )

        .def("state",      &Agent::state)
        .def("state_name", &Agent::stateName)
        .def("name",       &Agent::name)

        .def("context",
            [](Agent& a) -> ContextBuffer& { return a.context(); },
            py::return_value_policy::reference_internal)

        .def("tools",
            [](Agent& a) -> ToolRegistry& { return a.tools(); },
            py::return_value_policy::reference_internal)

        .def("__repr__", [](const Agent& a) {
            return "<CoreAgent name='" + a.name()
                 + "' state=" + a.stateName() + ">";
        });

    /* ── Module-level helpers ─────────────────────────── */
    m.def("version", []() { return "0.1.0"; });
    m.def("state_name", [](AgentState s) {
        return agentStateToString(s);
    });
}