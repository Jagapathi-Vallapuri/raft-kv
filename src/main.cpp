#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <grpcpp/grpcpp.h>
#include "RaftNode.h"

std::string parseCommandLineArgs(char **begin, char **end, const std::string& option) {
    char **itr = std::find(begin, end, option);
    if( itr != end && ++itr != end )
        return *itr;
    return "";  
}

void parseArgs( int argc, char* argv[], int& id, int& port, std::vector<std::string>& peers){
    for(int i = 1; i < argc; ++i){
        std::string arg = argv[i];
        if(arg.find("--id=") == 0)
            id = std::stoi(arg.substr(5));
        else if(arg.find("--port=") == 0)
            port = std::stoi(arg.substr(7));
        else if (arg.find("--peers=") == 0) {
            std::string peersStr = arg.substr(8);
            std::stringstream ss(peersStr);
            std::string peer;
            while (std::getline(ss, peer, ',')) {
                peers.push_back(peer);
            }
        }
    }
}

void RunRaftNode(RaftNode* node){
    node->Run();
}

int main(int argc, char* argv[]){
    int id = 0;
    int port = 50051;
    std::vector<std::string> peers;

    parseArgs(argc, argv, id, port, peers);
    
    if(id == 0){
        std::cerr << "Node ID must be specified with --id=" << std::endl;
        return 1;
    }

    std::string server_address("0.0.0.0:" + std::to_string(port));

    RaftNode node(id, server_address, peers);
    std::thread raft_thread(RunRaftNode, &node);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&node);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "[Node " << id << "] Server listening on " << server_address << std::endl;
    std::cout << "[Node " << id << "] Peers: ";

    for(const auto& peer : peers){
        std::cout << peer << " ";
    }
    std::cout << std::endl;

    server->Wait();

    if(raft_thread.joinable()){
        raft_thread.join();
    }

    return 0;
}