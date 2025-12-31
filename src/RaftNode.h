#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <random>
#include <chrono>
#include "raft.grpc.pb.h"
#include "raft.pb.h"

enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

class RaftNode final : public raft::RaftService::Service {
public:
    RaftNode(int id, std::string address, std::vector<std::string> peers);
    ~RaftNode();

    void Run();

    grpc::Status RequestVote(grpc::ServerContext* context, 
                             const raft::RequestVoteArgs* request, 
                             raft::RequestVoteReply* reply) override;

    grpc::Status AppendEntries(grpc::ServerContext* context, 
                               const raft::AppendEntriesArgs* request, 
                               raft::AppendEntriesReply* reply) override;

    // Fixed: Using correct Proto message names
    grpc::Status SubmitCommand(grpc::ServerContext* context, 
                               const raft::CommandRequest* request, 
                               raft::CommandReply* reply) override;

private:
    int current_term;
    int voted_for;
    std::vector<raft::LogEntry> log;

    int commit_index;
    int last_applied;

    std::vector<int> next_index; 
    std::vector<int> match_index; 

    int node_id;
    NodeState state;
    std::string node_address;
    std::vector<std::string> peer_addresses;

    std::mutex mtx; 
    
    std::chrono::time_point<std::chrono::steady_clock> last_heartbeat; 
    std::mt19937 rng; 

    void becomeFollower(int term);
    void startElection();
    void sendHeartbeats();
};