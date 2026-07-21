# benchmarks/bench_vs_langchain.py
import sys
import time
import resource
import gc

def get_memory_mb():
    # RUSAGE_SELF returns kilobytes on Linux
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1024.0

def measure_coreagent():
    print("=== CoreAgent Orchestration (Isolated) ===")
    gc.collect()
    mem_start = get_memory_mb()
    start_time = time.perf_counter()

    from coreagent.agent import Agent
    from coreagent._coreagent_cpp import AgentConfig

    # 1. Shrink the C++ arena sizes down to 1MB to destroy the 50MB baseline RSS
    config = AgentConfig()
    config.context_arena_capacity = 1024 * 1024    # 1 MB
    config.context_scratch_capacity = 1024 * 1024  # 1 MB
    
    agent = Agent(config=config)
    agent.context.add_system("You are a high-performance bare-metal orchestrated agent.")

    for i in range(1000):
        agent.context.add_user(f"Data payload {i}: " + "x" * 150)
        agent.context.add_assistant(f"Processed payload {i}.")

    # Decode the new zero-copy memoryview returned by the C++ engine
    prompt_view = agent.context.build_prompt()
    prompt = bytes(prompt_view).decode('utf-8')

    end_time = time.perf_counter()
    
    # 2. Force Python to GC the memoryview objects before calculating final RSS
    gc.collect()
    mem_end = get_memory_mb()

    print(f"Time-to-Ready (TTFA) : {(end_time - start_time) * 1000:.3f} ms")
    print(f"Memory Footprint (RSS): {mem_end - mem_start:.2f} MB")
    print(f"Final Prompt Output   : {len(prompt)} bytes")
    print("==========================================\n")

def measure_langchain():
    print("=== LangChain Orchestration (Isolated) ===")
    gc.collect()
    mem_start = get_memory_mb()
    start_time = time.perf_counter()

    from langchain_core.messages import SystemMessage, HumanMessage, AIMessage

    messages = [SystemMessage(content="You are a high-performance bare-metal orchestrated agent.")]

    for i in range(1000):
        messages.append(HumanMessage(content=f"Data payload {i}: " + "x" * 150))
        messages.append(AIMessage(content=f"Processed payload {i}."))

    current_chars = sum(len(m.content) for m in messages)
    while current_chars > 16384 and len(messages) > 1:
        dropped = messages.pop(1)
        current_chars -= len(dropped.content)

    prompt = "\n".join([f"[{m.type.upper()}]: {m.content}" for m in messages])

    end_time = time.perf_counter()
    
    gc.collect()
    mem_end = get_memory_mb()

    print(f"Time-to-Ready (TTFA) : {(end_time - start_time) * 1000:.3f} ms")
    print(f"Memory Footprint (RSS): {mem_end - mem_start:.2f} MB")
    print(f"Final Prompt Output   : {len(prompt)} bytes")
    print("==========================================\n")

if __name__ == "__main__":
    if len(sys.argv) == 1:
        import subprocess
        subprocess.run([sys.executable, sys.argv[0], "--coreagent"])
        subprocess.run([sys.executable, sys.argv[0], "--langchain"])
    elif sys.argv[1] == "--coreagent":
        measure_coreagent()
    elif sys.argv[1] == "--langchain":
        measure_langchain()