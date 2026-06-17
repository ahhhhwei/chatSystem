all : put get registry discovery
registry : registry.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -lgflags -letcd-cpp-api -lcpprest 
discovery : discovery.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -lgflags -letcd-cpp-api -lcpprest 
put : put.cc
	g++ -o $@ $^ -std=c++17 -letcd-cpp-api -lcpprest
get : get.cc
	g++ -o $@ $^ -std=c++17 -letcd-cpp-api -lcpprest
.PHONY:clean
clean:
	rm -f put get registry discovery