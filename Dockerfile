#build stage

FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler-grpc \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

#runtime stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y \
    libgrpc++1.51 \
    libprotobuf32 \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/raft_server /app/raft_server

EXPOSE 50051
CMD ["./raft_server"]