FROM node:22-bookworm-slim AS dashboard

WORKDIR /app/dashboard

COPY dashboard/package*.json ./
RUN npm ci

COPY dashboard/tsconfig.json ./
COPY dashboard/src ./src

# Terminal metrics dashboard. Defaults:
# RABBITMQ_URL=amqp://localhost:5672
# QUEUE_NAME=trade_metrics_c
CMD ["npm", "start"]

FROM python:3.13-slim AS worker

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        git \
        ninja-build \
        perl \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

RUN python -m pip install --no-cache-dir cmake conan

WORKDIR /app

COPY CMakeLists.txt CMakePresets.json conanfile.txt conan_provider.cmake ./
COPY include ./include
COPY src ./src

RUN conan profile detect --force
RUN cmake -S . -B /app/build -G Ninja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build /app/build --parallel

# Current archived C++ prototype:
# - connects to Kraken's public WebSocket feed
# - publishes telemetry to RabbitMQ queue/routing key trade_metrics_c
# If RabbitMQ is outside the container, run on Linux with --network host or
# update src/main.cpp to read the AMQP URL from an environment variable.
CMD ["/app/build/trading_infrastructure"]
