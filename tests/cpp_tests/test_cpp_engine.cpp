#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <atomic>
#include <cctype>
#include "Arena.hpp"
#include "ThreadPool.hpp"
#include "Executor.hpp"
#include "ToolRegistry.hpp"
#include "ContextBuffer.hpp"
#include "Agent.hpp"

using namespace coreagent;

/* ── Test: Arena RAII wrapper ────────────────────────────── */
void test_arena_cpp() {
    printf("\n[TEST] C++ Arena Wrapper\n");

    Arena arena(1024 * 1024);

    int* nums = (int*)arena.alloc(10 * sizeof(int));
    assert(nums != nullptr);
    for (int i = 0; i < 10; i++) nums[i] = i;
    assert(nums[9] == 9);

    auto mark = arena.save();
    arena.alloc(512);
    arena.restore(mark);
    assert(arena.used() == (size_t)mark);

    arena.printStats();
    printf("[TEST] C++ Arena Wrapper PASSED ✅\n");
}

/* ── Test: ThreadPool RAII wrapper ───────────────────────── */
static std::atomic<int> cpp_task_count{0};

void test_thread_pool_cpp() {
    printf("\n[TEST] C++ ThreadPool Wrapper\n");

    ThreadPool pool(4);
    cpp_task_count = 0;

    auto task = [](void* arg) {
        (void)arg;
        cpp_task_count.fetch_add(1);
    };

    for (int i = 0; i < 50; i++)
        pool.submit(task, nullptr);

    pool.wait();

    assert(cpp_task_count == 50);
    printf("[TEST] C++ ThreadPool: %d tasks done ✅\n",
           cpp_task_count.load());
}

/* ── Test: Executor wrapper ──────────────────────────────── */
void test_executor_cpp() {
    printf("\n[TEST] C++ Executor Wrapper\n");

    auto r1 = Executor::shell("echo 'CoreAgent CPP'", 2000);
    assert(r1.ok());
    assert(r1.stdout_data.find("CoreAgent CPP") != std::string::npos);
    printf("[TEST] shell output: %s", r1.stdout_data.c_str());

    auto r2 = Executor::shell("sleep 10", 300);
    assert(r2.timeout());
    printf("[TEST] Timeout correctly caught ✅\n");

    printf("[TEST] C++ Executor Wrapper PASSED ✅\n");
}

/* ── Test: ToolRegistry ──────────────────────────────────── */
void test_tool_registry() {
    printf("\n[TEST] ToolRegistry\n");

    ToolRegistry registry;

    registry.registerTool("add",
        "Add two numbers. args = 'a b'",
        [](const ToolInput& input) -> ToolOutput {
            int a = 0, b = 0;
            sscanf(input.args.c_str(), "%d %d", &a, &b);
            return ToolOutput{std::to_string(a + b), true, "", 0};
        }
    );

    registry.registerTool("upper",
        "Uppercase a string",
        [](const ToolInput& input) -> ToolOutput {
            std::string out = input.args;
            for (auto& c : out) c = toupper(c);
            return ToolOutput{out, true, "", 0};
        }
    );

    assert(registry.size() == 2);
    assert(registry.hasTool("add"));
    assert(registry.hasTool("upper"));

    auto r1 = registry.dispatch({"add", "7 3"});
    assert(r1.success);
    assert(r1.result == "10");
    printf("[TEST] add tool: 7 + 3 = %s ✅\n", r1.result.c_str());

    auto r2 = registry.dispatch({"upper", "coreagent"});
    assert(r2.success);
    assert(r2.result == "COREAGENT");
    printf("[TEST] upper tool: coreagent → %s ✅\n", r2.result.c_str());

    auto r3 = registry.dispatch({"nonexistent", ""});
    assert(!r3.success);
    printf("[TEST] unknown tool caught ✅\n");

    registry.printAll();
    printf("[TEST] ToolRegistry PASSED ✅\n");
}

/* ── Test: ContextBuffer ─────────────────────────────────── */
void test_context_buffer() {
    printf("\n[TEST] ContextBuffer\n");

    ContextBuffer ctx(4096, 4096, 200); /* Small limit to test trimming */

    ctx.addSystem("You are a helpful agent.");
    ctx.addUser("What is 2 + 2?");
    ctx.addAssistant("The answer is 4.");
    ctx.addToolResult("calculator", "4");

    assert(ctx.messageCount() == 4);

    std::string prompt(ctx.buildPrompt());
    assert(prompt.find("SYSTEM") != std::string::npos);
    assert(prompt.find("USER")   != std::string::npos);
    assert(prompt.find("TOOL:calculator") != std::string::npos);

    printf("[TEST] Prompt preview:\n%s\n", prompt.c_str());

    ctx.printSummary();
    printf("[TEST] ContextBuffer PASSED ✅\n");
}

/* ── Test: Full Agent Loop ───────────────────────────────── */
void test_agent() {
    printf("\n[TEST] Agent State Machine\n");

    AgentConfig config;
    config.name        = "TestAgent";
    config.max_steps   = 3;
    config.num_threads = 2;

    Agent agent(config);
    agent.tools().printAll();

    /* Test 1: list files task */
    std::string r1 = agent.run("list the files in current directory");
    assert(!r1.empty());
    printf("[TEST] Task 1 output length: %zu chars ✅\n", r1.size());

    /* Test 2: get current date */
    std::string r2 = agent.run("what is the current date and time");
    assert(!r2.empty());
    printf("[TEST] Task 2 output: %s ✅\n", r2.c_str());

    /* Test 3: custom tool */
    agent.registerTool("greet",
        "Greet someone. args = name",
        [](const ToolInput& in) -> ToolOutput {
            return ToolOutput{"Hello, " + in.args + "! 👋", true, "", 0};
        }
    );
    assert(agent.tools().hasTool("greet"));
    auto gr = agent.tools().dispatch({"greet", "CoreAgent"});
    assert(gr.result == "Hello, CoreAgent! 👋");
    printf("[TEST] Custom tool: %s ✅\n", gr.result.c_str());

    printf("[TEST] Agent State Machine PASSED ✅\n");
}

/* ── Main ────────────────────────────────────────────────── */
int main() {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   CoreAgent C++ Engine Tests              ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    test_arena_cpp();
    test_thread_pool_cpp();
    test_executor_cpp();
    test_tool_registry();
    test_context_buffer();
    test_agent();

    printf("\n✅ All C++ Engine tests PASSED!\n\n");
    return 0;
}