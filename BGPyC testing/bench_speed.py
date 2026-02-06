import argparse
import time

import bgpyc
import bgpyp


def bench(label, func, loops):
    start = time.perf_counter()
    for _ in range(loops):
        func()
    elapsed = time.perf_counter() - start
    print(f"{label}: {elapsed:.6f} s")


def main():
    parser = argparse.ArgumentParser(description="Speed test bgpyc vs pure Python mirror.")
    parser.add_argument("--loops", type=int, default=1_000_000)
    args = parser.parse_args()

    c_as = bgpyc.AS(64512)
    py_as = bgpyp.AS(64512)

    # Warm up
    for _ in range(1000):
        c_as.step()
        py_as.step()

    print(f"loops: {args.loops}")
    bench("bgpyc step", c_as.step, args.loops)
    bench("bgpyc call_step", lambda: bgpyc.call_step(c_as), args.loops)
    bench("bgpyp step", py_as.step, args.loops)
    bench("bgpyp call_step", lambda: bgpyp.call_step(py_as), args.loops)


if __name__ == "__main__":
    main()
