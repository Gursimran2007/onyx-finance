# ── Stage 1: Build ──────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    libasio-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy dependency sources
COPY libs/ libs/

# Copy source
COPY backend/ backend/
COPY CMakeLists.txt .

# Build
RUN mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release -DSQLITECPP_RUN_CPPLINT=OFF \
    && make -j$(nproc)

# ── Stage 2: Runtime ─────────────────────────────────
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    libcurl4-openssl-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/finance_app .
COPY frontend/ frontend/

EXPOSE 8080

CMD ["./finance_app"]
