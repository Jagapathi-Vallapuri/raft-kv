#include "RaftNode.h"
#include <chrono>
#include <random>
#include <iostream>
#include <algorithm>

using namespace std::chrono;

RaftNode::RaftNode(int id, std::string address, std::vector<std::string> peers)
    : node_id(id), node_address(address), peer_addresses(peers),
      current_term(0), voted_for(-1), commit_index(0), last_applied(0),
      state(NodeState::FOLLOWER) {
    
    for(size_t i = 0; i < peers.size(); ++i){
        next_index.push_back(1);
        match_index.push_back(0);
    }
    last_heartbeat = steady_clock::now();
}

RaftNode::~RaftNode() {}

void RaftNode::Run(){
    std::random_device rd;
    rng = std::mt19937(rd());
    std::uniform_int_distribution<int> distribution(150, 300);

    while(true){
        std::this_thread::sleep_for(milliseconds(10));
        
        int timeout_ms = distribution(rng);
        std::unique_lock<std::mutex> lock(mtx);

        if(state == NodeState::LEADER){
            sendHeartbeats();
            lock.unlock();
            std::this_thread::sleep_for(milliseconds(50));
            continue;
        }

        auto now = steady_clock::now();
        auto elapsed = duration_cast<milliseconds>(now - last_heartbeat).count();

        if(elapsed > timeout_ms){
            std::cout << "[Node " << node_id << "] Election timeout! ("
                      << elapsed << "ms)" << std::endl;
            lock.unlock();
            startElection();
        }
    }
}

void RaftNode::becomeFollower(int term) {
    state = NodeState::FOLLOWER;
    current_term = term;
    voted_for = -1;
    last_heartbeat = steady_clock::now();
    std::cout << "[Node " << node_id << "] Stepping down to FOLLOWER (Term " << term << ")" << std::endl;
}

void RaftNode::startElection(){
    std::lock_guard<std::mutex> lock(mtx);
    
    state = NodeState::CANDIDATE;
    current_term++;
    voted_for = node_id;
    int votes_received = 1;

    std::cout << "[Node " << node_id << "] Starting election for term " << current_term << std::endl;
    last_heartbeat = steady_clock::now();

    for(const auto& peer : peer_addresses){
        raft::RequestVoteArgs args;
        args.set_term(current_term);
        args.set_candidate_id(node_id);
        args.set_last_log_index(log.empty() ? 0 : log.size() - 1);
        args.set_last_log_term(log.empty() ? 0 : log.back().term());

        std::thread([this, peer, args, &votes_received]() {
            auto channel = grpc::CreateChannel(peer, grpc::InsecureChannelCredentials());
            auto stub = raft::RaftService::NewStub(channel);

            raft::RequestVoteReply reply;
            grpc::ClientContext context;
            grpc::Status status = stub->RequestVote(&context, args, &reply);

            if(status.ok()){
                std::lock_guard<std::mutex> lock(mtx);
                if(state != NodeState::CANDIDATE) return;

                if(reply.term() > current_term){
                    becomeFollower(reply.term());
                    return;
                }

                if(reply.vote_granted()){
                    votes_received++;
                    if(votes_received > (peer_addresses.size() + 1) / 2){
                        state = NodeState::LEADER;
                        std::cout << "[Node " << node_id << "] Became LEADER for term " << current_term << std::endl;
                        for(size_t i=0; i<next_index.size(); i++) {
                            next_index[i] = log.size() + 1;
                            match_index[i] = 0;
                        }
                    }
                }
            }
        }).detach();
    }
}

grpc::Status RaftNode::RequestVote(grpc::ServerContext* context, 
                                   const raft::RequestVoteArgs* request, 
                                   raft::RequestVoteReply* reply){
    std::lock_guard<std::mutex> lock(mtx);

    if(request->term() < current_term){
        reply->set_term(current_term);
        reply->set_vote_granted(false);
        return grpc::Status::OK;
    }

    if(request->term() > current_term){
        becomeFollower(request->term());
    }

    int last_log_index = log.empty() ? 0 : log.size() - 1;
    int last_log_term = log.empty() ? 0 : log.back().term();

    bool is_log_ok = false;
    if(request->last_log_term() > last_log_term){
        is_log_ok = true;
    }
    else if(request->last_log_term() == last_log_term && 
            request->last_log_index() >= last_log_index){
        is_log_ok = true;
    }

    if((voted_for == -1 || voted_for == request->candidate_id()) && is_log_ok){
        voted_for = request->candidate_id();
        reply->set_term(current_term);
        reply->set_vote_granted(true);
        state = NodeState::FOLLOWER;
        last_heartbeat = steady_clock::now();
    }
    else{
        reply->set_term(current_term);
        reply->set_vote_granted(false);
    }
    return grpc::Status::OK;
}

void RaftNode::sendHeartbeats(){
    for(size_t i = 0; i < peer_addresses.size(); ++i){
        raft::AppendEntriesArgs args;
        args.set_term(current_term);
        args.set_leader_id(node_id);
        args.set_leader_commit(commit_index);

        int prev_index = next_index[i] - 1;
        args.set_prev_log_index(prev_index);

        if(prev_index >= 0 && prev_index < log.size())
            args.set_prev_log_term(log[prev_index].term());
        else
            args.set_prev_log_term(0);

        std::string peer = peer_addresses[i]; 

        std::thread([this, i, peer, args]() {
            auto channel = grpc::CreateChannel(peer, grpc::InsecureChannelCredentials());
            auto stub = raft::RaftService::NewStub(channel);

            raft::AppendEntriesReply reply;
            grpc::ClientContext context;
            grpc::Status status = stub->AppendEntries(&context, args, &reply);

            if(!status.ok()) return;

            std::lock_guard<std::mutex> lock(mtx);
            if(reply.term() > current_term){
                becomeFollower(reply.term());
                return;
            }
            if(state != NodeState::LEADER) return;

            if(reply.success()){
                match_index[i] = args.prev_log_index() + args.entries_size();
                next_index[i] = match_index[i] + 1;
            }
            else{
                if(next_index[i] > 0) next_index[i]--;
            }
        }).detach();
    }
}

grpc::Status RaftNode::AppendEntries(grpc::ServerContext* context,
                                     const raft::AppendEntriesArgs* request,
                                     raft::AppendEntriesReply* reply) {
    std::lock_guard<std::mutex> lock(mtx);

    if(request->term() < current_term){
        reply->set_success(false);
        reply->set_term(current_term);
        return grpc::Status::OK;
    }

    state = NodeState::FOLLOWER;
    current_term = request->term();
    voted_for = -1;
    last_heartbeat = steady_clock::now();

    int last_index = log.empty() ? -1 : log.size() - 1;
    if(request->prev_log_index() > last_index){
        reply->set_success(false);
        reply->set_term(current_term);
        return grpc::Status::OK;
    }

    if(request->leader_commit() > commit_index)
        commit_index = std::min((long long)request->leader_commit(), (long long)log.size() - 1);
    
    reply->set_success(true);
    reply->set_term(current_term);
    return grpc::Status::OK;
}

grpc::Status RaftNode::SubmitCommand(grpc::ServerContext* context,
                                     const raft::CommandRequest* request,
                                     raft::CommandReply* reply) {
    std::lock_guard<std::mutex> lock(mtx);

    if(state == NodeState::LEADER){
        raft::LogEntry new_entry;
        new_entry.set_term(current_term);
        new_entry.set_command(request->command());
        log.push_back(new_entry);

        std::cout << "[Node " << node_id << "] Appended command: " << request->command() << std::endl;

        reply->set_success(true);
        reply->set_leader_id(node_id);
        reply->set_message("Command appended.");
        return grpc::Status::OK;
    }

    reply->set_success(false);
    if(voted_for != -1 && voted_for != node_id)
        reply->set_leader_id(voted_for);
    else
        reply->set_leader_id(-1);
        
    reply->set_message("Not Leader");
    return grpc::Status::OK;
}