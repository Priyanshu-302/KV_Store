#!/usr/bin/env python3
"""
High-Performance Asynchronous Benchmark Load Tester
Evaluates throughput (QPS) and tail latency percentiles (p50, p95, p99, p99.9)
against the C++ Key-Value Store using standard Python asyncio (zero external dependencies).
"""

import asyncio
import time
import argparse
import random
import string
import sys
from typing import List

def encode_resp_command(*args: str) -> bytes:
    """Encodes arguments into official Redis Serialization Protocol (RESP v2) wire bytes."""
    buf = [f"*{len(args)}\r\n".encode("ascii")]
    for arg in args:
        encoded = arg.encode("utf-8")
        buf.append(f"${len(encoded)}\r\n".encode("ascii"))
        buf.append(encoded)
        buf.append(b"\r\n")
    return b"".join(buf)

def generate_random_payload(size: int) -> str:
    """Generates random alphanumeric string payload."""
    return ''.join(random.choices(string.ascii_letters + string.digits, k=size))

async def worker(
    worker_id: int,
    host: str,
    port: int,
    requests_per_worker: int,
    workload: str,
    key_size: int,
    val_size: int,
    pipeline_depth: int,
    latencies: List[float]
):
    """Asynchronous client connection worker sending pipelined or standalone requests."""
    try:
        reader, writer = await asyncio.open_connection(host, port)
    except Exception as e:
        print(f"[Worker {worker_id}] Connection failed: {e}", file=sys.stderr)
        return

    val = generate_random_payload(val_size)
    reqs_sent = 0

    while reqs_sent < requests_per_worker:
        batch_size = min(pipeline_depth, requests_per_worker - reqs_sent)
        batch_bytes = bytearray()
        
        for _ in range(batch_size):
            key = f"key:{worker_id}:{random.randint(0, 10000)}"
            if workload == "set":
                batch_bytes.extend(encode_resp_command("SET", key, val))
            elif workload == "get":
                batch_bytes.extend(encode_resp_command("GET", key))
            else: # mixed 50/50
                if random.random() < 0.5:
                    batch_bytes.extend(encode_resp_command("SET", key, val))
                else:
                    batch_bytes.extend(encode_resp_command("GET", key))

        t_start = time.perf_counter_ns()
        writer.write(batch_bytes)
        await writer.drain()

        # Drain responses for batch
        for _ in range(batch_size):
            line = await reader.readline()
            if not line:
                break
            # If response is bulk string ($<len>), drain payload + CRLF
            if line.startswith(b"$") and not line.startswith(b"$-1"):
                payload_len = int(line[1:].strip())
                await reader.readexactly(payload_len + 2)

        t_end = time.perf_counter_ns()
        # Record average latency per request in batch (microseconds)
        elapsed_us = (t_end - t_start) / (batch_size * 1000.0)
        for _ in range(batch_size):
            latencies.append(elapsed_us)

        reqs_sent += batch_size

    writer.close()
    await writer.wait_closed()

async def main():
    parser = argparse.ArgumentParser(description="Async Redis Clone Benchmark Load Tester")
    parser.add_argument("--host", default="127.0.0.1", help="Server host IP (default: 127.0.0.1)")
    parser.add_argument("-p", "--port", type=int, default=6379, help="Server port (default: 6379)")
    parser.add_argument("-c", "--clients", type=int, default=50, help="Number of concurrent clients (default: 50)")
    parser.add_argument("-n", "--requests", type=int, default=100000, help="Total requests to send (default: 100000)")
    parser.add_argument("-w", "--workload", choices=["set", "get", "mixed"], default="mixed", help="Workload profile")
    parser.add_argument("-P", "--pipeline", type=int, default=1, help="Pipeline depth (batch size, default: 1)")
    parser.add_argument("--val-size", type=int, default=64, help="Value payload size in bytes (default: 64)")
    args = parser.parse_args()

    reqs_per_client = args.requests // args.clients
    total_reqs = reqs_per_client * args.clients

    print("==================================================================")
    print("  In-Memory Key-Value Store Benchmark (asyncio)                   ")
    print(f"  Target:            {args.host}:{args.port}")
    print(f"  Concurrent Clients: {args.clients}")
    print(f"  Total Requests:    {total_reqs}")
    print(f"  Workload Profile:  {args.workload.upper()}")
    print(f"  Pipeline Depth:    {args.pipeline}")
    print(f"  Payload Size:      {args.val_size} bytes")
    print("==================================================================")

    latencies: List[float] = []

    start_time = time.perf_counter()
    tasks = [
        worker(
            worker_id=i,
            host=args.host,
            port=args.port,
            requests_per_worker=reqs_per_client,
            workload=args.workload,
            key_size=16,
            val_size=args.val_size,
            pipeline_depth=args.pipeline,
            latencies=latencies
        )
        for i in range(args.clients)
    ]

    await asyncio.gather(*tasks)
    total_time = time.perf_counter() - start_time

    if not latencies:
        print("Error: No requests completed successfully!", file=sys.stderr)
        return

    latencies.sort()
    qps = len(latencies) / total_time
    avg_us = sum(latencies) / len(latencies)
    p50 = latencies[int(len(latencies) * 0.50)]
    p90 = latencies[int(len(latencies) * 0.90)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    p999 = latencies[int(len(latencies) * 0.999)]

    print("\n--- RESULTS ---")
    print(f"Total Completed:     {len(latencies)} requests in {total_time:.3f} seconds")
    print(f"Throughput:          {qps:.2f} requests/sec")
    print("\n--- LATENCY DISTRIBUTION ---")
    print(f"  Avg Latency:       {avg_us:.2f} us ({avg_us / 1000.0:.3f} ms)")
    print(f"  p50 (Median):      {p50:.2f} us ({p50 / 1000.0:.3f} ms)")
    print(f"  p90:               {p90:.2f} us ({p90 / 1000.0:.3f} ms)")
    print(f"  p95:               {p95:.2f} us ({p95 / 1000.0:.3f} ms)")
    print(f"  p99:               {p99:.2f} us ({p99 / 1000.0:.3f} ms)")
    print(f"  p99.9:             {p999:.2f} us ({p999 / 1000.0:.3f} ms)")
    print("==================================================================")

if __name__ == "__main__":
    asyncio.run(main())
