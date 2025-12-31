cmake_minimum_required(VERSION 3.15)
project(RaftKV)

set(CMAKE_CXX_STANDARD 17)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_SRC raft.proto)
set(PROTO_HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/raft.grpc.pb.h")
set(PROTO_SRC_OUT "${CMAKE_CURRENT_BINARY_DIR}/raft.grpc.pb.cc")
set(PROTO_PB_HEADER_OUT "${CMAKE_CURRENT_BINARY_DIR}/raft.pb.h")
set(PROTO_PB_SRC_OUT "${CMAKE_CURRENT_BINARY_DIR}/raft.pb.cc")

add_custom_command(
    OUTPUT "${PROTO_SRC_OUT}" "${PROTO_HEADER_OUT}" "${PROTO_PB_SRC_OUT}" "${PROTO_PB_HEADER_OUT}"
    COMMAND protoc
        --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${CMAKE_CURRENT_SOURCE_DIR}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        "${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_SRC}"
    DEPENDS "${PROTO_SRC}"
)

add_executable(raft_server
    src/main.cpp
    src/RaftNode.cpp
    "${PROTO_SRC_OUT}"
    "${PROTO_PB_SRC_OUT}"
)

target_link_libraries( raft_server
    PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
    pthread
    sqlite3
)

target_include_directories(raft_server PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")