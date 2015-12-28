#
# To compile, type "make" 
# To remove files, type "make clean"
#
LIBS = -lpthread 
all: wspace_client_scout

wspace_client_scout: wspace_client_scout.o wspace_asym_util.o time_util.o tun.o fec.o udp_socket.o gps_parser.o
	$(CXX) $(CXXFLAGS) $^ -o wspace_client_scout $(LIBS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -rf wspace_client_scout *.o 

tag: 
	ctags -R *
