/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include "eden/fs/benchharness/Bench.h"
#include "eden/fs/tracing/Tracing.h"

using namespace facebook::eden;

BENCHMARK(Tracer_repeatedly_create_trace_points, n) {
  {
    folly::BenchmarkSuspender suspender;
    enableTracing();
  }
  for (unsigned i = 0; i < n; ++i) {
    TraceBlock block{"foo"};
  }
}

BENCHMARK(Tracer_repeatedly_create_trace_points_from_multiple_threads, n) {
  constexpr unsigned threadCount = 8;
  std::vector<std::thread> threads;
  StartingGate gate{threadCount};
  {
    folly::BenchmarkSuspender suspender;
    enableTracing();

    for (unsigned i = 0; i < threadCount; ++i) {
      threads.emplace_back([n, &gate] {
        gate.wait();
        // We aren't measuring the time of these other threads, so
        // double the number of trace points to keep things busy
        // while the main thread creates the requested number of
        // tracepoints
        for (unsigned i = 0; i < n * 2; ++i) {
          TraceBlock block{"foo"};
        }
      });
    }
    gate.waitThenOpen();
  }
  for (unsigned i = 0; i < n; ++i) {
    TraceBlock block{"foo"};
  }
  folly::BenchmarkSuspender suspender;
  for (auto& thread : threads) {
    thread.join();
  }
}

BENCHMARK(Tracer_repeatedly_create_trace_points_disabled, n) {
  {
    folly::BenchmarkSuspender suspender;
    disableTracing();
  }
  for (unsigned i = 0; i < n; ++i) {
    TraceBlock block{"foo"};
  }
}

int main(int argc, char** argv) {
  folly::init(&argc, &argv);
  folly::runBenchmarks();
}
