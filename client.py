import grpc
import sys
import time
import random
import raft_pb2
import raft_pb2_grpc

# Maps Docker "ports" to localhost
NODES = { 1: 'localhost:50051', 2: 'localhost:50052', 3: 'localhost:50053' }

def send_command(cmd):
    leader_id = 1
    while True:
        address = NODES.get(leader_id)
        try:
            with grpc.insecure_channel(address) as channel:
                stub = raft_pb2_grpc.RaftServiceStub(channel)
                print(f"Connecting to Node {leader_id}...")
                response = stub.SubmitCommand(raft_pb2.CommandRequest(command=cmd))
                
                if response.success:
                    print(f"✅ SUCCESS! Node {response.leader_id} accepted: '{cmd}'")
                    break
                elif response.leader_id != -1:
                    print(f"🔄 Redirecting to Leader Node {response.leader_id}...")
                    leader_id = response.leader_id
                else:
                    print("⚠️ No Leader. Retrying...")
                    time.sleep(1)
                    leader_id = random.choice(list(NODES.keys()))
        except grpc.RpcError:
            print(f"❌ Node {leader_id} is down. Retrying...")
            leader_id = random.choice(list(NODES.keys()))
            time.sleep(1)

if __name__ == "__main__":
    send_command(sys.argv[1] if len(sys.argv) > 1 else "Hello Raft")