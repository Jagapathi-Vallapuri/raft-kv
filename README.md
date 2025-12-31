# RaftKV (C++ gRPC)

A small Raft-style cluster demo implemented in C++ with gRPC + Protobuf.

- **Server:** `raft_server` (C++) in `src/`
- **Protocol:** `raft.proto` (gRPC service + messages)
- **Client:** `client.py` (Python) that sends `SubmitCommand` and follows redirects

## Quickstart (Docker)

```bash
docker compose up --build
python client.py "set x=1"
```

Docker Compose publishes the three nodes on `localhost:50051`, `localhost:50052`, `localhost:50053`.

## What’s in the repo

- `src/main.cpp` — parses `--id`, `--port`, `--peers` and starts a gRPC server
- `src/RaftNode.{h,cpp}` — Raft-ish leader election + heartbeat loop and RPC handlers
- `src/RaftNode.{h,cpp}` — leader election + heartbeats + **attempted** log replication/commit + SQLite-backed state apply
- `raft.proto` — `RaftService` with `RequestVote`, `AppendEntries`, `SubmitCommand`
- `client.py` — example client (targets ports used by `docker-compose.yml`)
- `Dockerfile` / `docker-compose.yml` — build and run a 3-node cluster in containers

## Prerequisites (local build)

You need a C++17 toolchain, CMake, Protobuf, and gRPC.

On Debian/Ubuntu-like systems, the following packages match what the Docker build uses:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake \
  libgrpc++-dev libprotobuf-dev protobuf-compiler-grpc
```

## Build (local)

```bash
mkdir -p build
cmake -S . -B build
cmake --build build -j
```

This produces `build/raft_server`.

## Run a 3-node cluster (local)

Open three terminals (or run them in the background) and start each node with distinct ports and peer lists.

```bash
./build/raft_server --id=1 --port=50051 --peers=localhost:50052,localhost:50053
./build/raft_server --id=2 --port=50052 --peers=localhost:50051,localhost:50053
./build/raft_server --id=3 --port=50053 --peers=localhost:50051,localhost:50052
```

Notes:
- `--id` is required.
- `--peers` is a comma-separated list of `host:port` addresses.

## Run with Docker Compose

This brings up 3 containers (`node1`, `node2`, `node3`) and maps their internal gRPC port `50051` to local ports `50051`, `50052`, `50053`.

```bash
docker compose up --build
```

Stop everything with:

```bash
docker compose down
```

Note: `docker-compose.yml` defines per-node volumes under `/var/lib/raft_data`, but the current C++ implementation does not persist state there yet.

Update: the current `RaftNode` opens a SQLite DB under `/var/lib/raft_data/raft_node_<id>.db` inside the container. The Compose volumes make that path persistent across restarts.

## Python client

The Python client sends a `SubmitCommand` request and will retry / redirect to the leader if needed.

### Install dependencies

```bash
python -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install grpcio protobuf
```

### Send a command

If you’re running via Docker Compose (or locally on ports 50051/50052/50053):

```bash
python client.py "set x=1"
```

The client uses this mapping (see `client.py`):

- Node 1 → `localhost:50051`
- Node 2 → `localhost:50052`
- Node 3 → `localhost:50053`

## Proto generation

### C++

The C++ build generates protobuf + gRPC sources during the CMake build using `protoc` and the gRPC C++ plugin (see `CMakeLists.txt`). Generated files land under `build/`.

### Python

This repo already includes `raft_pb2.py` and `raft_pb2_grpc.py`. If you change `raft.proto`, regenerate them with `grpcio-tools`:

```bash
pip install grpcio-tools
python -m grpc_tools.protoc \
  -I . \
  --python_out=. \
  --grpc_python_out=. \
  raft.proto
```

## Troubleshooting

## Current behavior / limitations

This repo is a learning/demo implementation and does **not** implement the full Raft spec.

- **Election + heartbeats:** nodes run an election timer (~150–300ms) and the leader sends periodic `AppendEntries` heartbeats.
- **Log replication (partial):** the leader includes log entries in `AppendEntries`, and followers append received entries.
- **Commit + apply (partial):** the leader tries to advance `commit_index` when it believes a majority replicated, and each node applies committed entries by executing them as SQL against a local SQLite DB.
  - The DB path is `/var/lib/raft_data/raft_node_<id>.db` (created on startup).
  - The example table is `users(name TEXT, value INTEGER)`. Commands are treated as raw SQL.
- **Client semantics:** `client.py` calls `SubmitCommand` and treats `success=true` as “accepted by leader”. In the current server, that does **not** necessarily mean the command was committed/applied cluster-wide.

Known limitations / rough edges (current code):

- **Indexing inconsistencies:** `prev_log_index`/`next_index` and the follower’s `AppendEntries` validation appear to be using different base conventions (0-based vs 1-based), which can cause followers to reject initial replication/heartbeats when logs are empty.
- **No conflict handling:** followers don’t verify `prev_log_term` or delete conflicting log entries; they simply append.
- **Leader discovery is best-effort:** non-leaders return a `leader_id` based on local `voted_for`, which is not a reliable “current leader” pointer.
- **No persistence of Raft metadata:** term/vote/log are not replayed from disk on restart (only the SQLite DB file persists).
- **No snapshots / membership changes / client read API**.

- **Build fails finding gRPC/Protobuf:** ensure the dev packages are installed (or use Docker).
- **Nodes can’t reach peers:** verify the `--peers` addresses match the ports you started each node on.
- **Client can’t connect:** make sure the cluster is running and ports `50051`–`50053` are reachable.
