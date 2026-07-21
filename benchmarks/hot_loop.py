# benchmarks/hot_loop.py
import sys
import time
import resource
import gc

ITERATIONS = 50000
TOKEN_LIMIT_CHARS = 16384  # Roughly 4096 tokens

def get_memory_mb():
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0

def run_coreagent_hot_loop():
    print("=== CoreAgent Hot Loop (50,000 Iterations) ===")
    gc.collect()
    mem_start = get_memory_mb()

    from coreagent.agent import Agent
    from coreagent._coreagent_cpp import AgentConfig

    # 1. Shrink baseline memory footprint
    config = AgentConfig()
    config.context_arena_capacity = 1024 * 1024
    config.context_scratch_capacity = 1024 * 1024
    
    agent = Agent(config=config)
    agent.context.add_system("System: Execute sustained processing loop.")

    start_time = time.perf_counter()

    for i in range(ITERATIONS):
        agent.context.add_user(f"Task {i}: Calculate and return data parameters.")
        agent.context.add_assistant(f"Result {i}: Data processed successfully.")
        
        # Simulate the LLM request trigger every 10 steps
        if i % 10 == 0:
            prompt_view = agent.context.build_prompt()
            # Do not hold onto prompt_view to allow Pybind11 memory to be freed 

    end_time = time.perf_counter()
    
    # 2. Force GC to collect the 50,000 dead memoryview objects created during the loop
    gc.collect()
    mem_end = get_memory_mb()

    total_time = end_time - start_time
    print(f"Total Time        : {total_time:.3f} seconds")
    print(f"Throughput        : {ITERATIONS / total_time:.0f} iterations/sec")
    print(f"Added Memory (RSS): {mem_end - mem_start:.2f} MB")
    print("==============================================\n")

def run_langchain_hot_loop():
    print("=== LangChain Hot Loop (50,000 Iterations) ===")
    gc.collect()
    mem_start = get_memory_mb()

    from langchain_core.messages import SystemMessage, HumanMessage, AIMessage

    messages = [SystemMessage(content="System: Execute sustained processing loop.")]
    current_chars = len(messages[0].content)

    start_time = time.perf_counter()

    for i in range(ITERATIONS):
        h_msg = HumanMessage(content=f"Task {i}: Calculate and return data parameters.")
        a_msg = AIMessage(content=f"Result {i}: Data processed successfully.")
        
        messages.append(h_msg)
        messages.append(a_msg)
        current_chars += len(h_msg.content) + len(a_msg.content)

        # Python list shift penalty: manual trim
        while current_chars > TOKEN_LIMIT_CHARS and len(messages) > 1:
            dropped = messages.pop(1)
            current_chars -= len(dropped.content)

        # Python heap allocation penalty: format prompt
        if i % 10 == 0:
            _ = "\n".join([f"[{m.type.upper()}]: {m.content}" for m in messages])

    end_time = time.perf_counter()
    
    gc.collect()
    mem_end = get_memory_mb()

    total_time = end_time - start_time
    print(f"Total Time        : {total_time:.3f} seconds")
    print(f"Throughput        : {ITERATIONS / total_time:.0f} iterations/sec")
    print(f"Added Memory (RSS): {mem_end - mem_start:.2f} MB")
    print("==============================================\n")

if __name__ == "__main__":
    if len(sys.argv) == 1:
        import subprocess
        subprocess.run([sys.executable, sys.argv[0], "--coreagent"])
        subprocess.run([sys.executable, sys.argv[0], "--langchain"])
    elif sys.argv[1] == "--coreagent":
        run_coreagent_hot_loop()
    elif sys.argv[1] == "--langchain":
        run_langchain_hot_loop()