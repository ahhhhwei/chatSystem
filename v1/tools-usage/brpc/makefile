all : server client test
test : test.cc
	g++ -std=c++17 $^ -o $@ -lbrpc -lgflags -lssl -lcrypto -lprotobuf -lleveldb -ldl -pthread 
server : server.cc main.pb.cc
	g++ -std=c++17 $^ -o $@ -lbrpc -lgflags -lssl -lcrypto -lprotobuf -lleveldb -ldl -pthread 
client : client.cc main.pb.cc
	g++ -std=c++17 $^ -o $@ -lbrpc -lgflags -lssl -lcrypto -lprotobuf -lleveldb -ldl -pthread