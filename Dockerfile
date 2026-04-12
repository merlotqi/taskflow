# syntax=docker/dockerfile:1.6
# TaskFlow C++ orchestration library — build, test, install, and optional smoke demo.

ARG DEBIAN_VERSION=bookworm-slim

# -----------------------------------------------------------------------------
# Builder: compile library, run tests, install to /opt/taskflow
# -----------------------------------------------------------------------------
FROM debian:${DEBIAN_VERSION} AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    ca-certificates \
    libsqlite3-dev \
    libnlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

ARG CMAKE_BUILD_TYPE=Release
ARG TASKFLOW_BUILD_TESTS=ON
ARG TASKFLOW_BUILD_BENCHMARKS=OFF
ARG TASKFLOW_BUILD_EXAMPLES=ON

RUN cmake -S . -B /build -G Ninja \
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" \
    "-DTASKFLOW_BUILD_TESTS=${TASKFLOW_BUILD_TESTS}" \
    "-DTASKFLOW_BUILD_BENCHMARKS=${TASKFLOW_BUILD_BENCHMARKS}" \
    "-DTASKFLOW_BUILD_EXAMPLES=${TASKFLOW_BUILD_EXAMPLES}" \
    && cmake --build /build --parallel \
    && if [ "${TASKFLOW_BUILD_TESTS}" = "ON" ]; then ctest --test-dir /build --output-on-failure; fi \
    && cmake --install /build --prefix /opt/taskflow

# Minimal demo binary for runtime smoke test (optional)
RUN mkdir -p /opt/taskflow/bin \
    && cp /build/examples/01_minimal_task /opt/taskflow/bin/taskflow-smoke-demo

# -----------------------------------------------------------------------------
# Runtime: headers + libs + demo (Debian matches builder glibc)
# -----------------------------------------------------------------------------
FROM debian:${DEBIAN_VERSION} AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/taskflow /usr/local

LABEL org.opencontainers.image.title="TaskFlow"
LABEL org.opencontainers.image.description="Lightweight C++ task orchestration engine (library + smoke demo)"
LABEL org.opencontainers.image.source="https://github.com/merlotqi/taskflow"

# Default: run the smallest bundled example
CMD ["/usr/local/bin/taskflow-smoke-demo"]
