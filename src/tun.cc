#include "tun.h"

using namespace std;
int Tun::AllocTun(char *dev, int flags)
{
  	struct ifreq ifr;
  	int fd, err;
  	char *clonedev = "/dev/net/tun";

  	if( (fd = open(clonedev , O_RDWR)) < 0 )
	{
   		perror("Opening /dev/net/tun");
		return fd;
  	}

  	memset(&ifr, 0, sizeof(ifr));

  	ifr.ifr_flags = flags;

  	if (*dev)
	{
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
  	}

  	if((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 )
	{
		perror("ioctl(TUNSETIFF)");
		close(fd);
		return err;
	}

	strcpy(dev, ifr.ifr_name);
	return fd;
}

/** For dst.*/
void Tun::CreateAddr(const char *ip, int port, sockaddr_in *addr)
{
	memset(addr, 0, sizeof(sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(ip);
	addr->sin_port = htons(port);
}

/** For self.*/
void Tun::CreateAddr(int port, sockaddr_in *addr)
{
	memset(addr, 0, sizeof(sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl(INADDR_ANY);
	addr->sin_port = htons(port);
}

void Tun::BindSocket(int fd, sockaddr_in *addr)
{
	int optval = 1;
	socklen_t addr_len = sizeof(struct sockaddr_in);

	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&optval, sizeof(optval)) < 0) 
		perror("setsocketopt()");

	if(bind(fd, (struct sockaddr*)addr, addr_len) < 0) 
		perror("ath bind()");
}

int Tun::CreateSock()
{
	int sock_fd;
	if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
		perror("UDP socket()");
		exit(-1);
	}
	return sock_fd;
}

void Tun::InitSock()
{
	/* initialize tun/tap interface */
	if ( (tun_fd_ = AllocTun(if_name_, tun_type_ | IFF_NO_PI)) < 0 )
	{
		perror("Error connecting to tun/tap interface");
	}

	// Create sockets
	sock_fd_eth_ = CreateSock();
	sock_fd_ath_ = CreateSock();

	// Self address
	CreateAddr(port_eth_, &client_addr_eth_); 	
	CreateAddr(port_ath_, &client_addr_ath_); 	
	// Their address
	CreateAddr(server_ip_eth_, port_eth_, &server_addr_eth_);

	BindSocket(sock_fd_eth_, &client_addr_eth_);
	BindSocket(sock_fd_ath_, &client_addr_ath_);
}

void Tun::CreateConn()
{
	InitSock();
	InformServerAddr(sock_fd_eth_, &server_addr_eth_);
	relay_sock_.SocketSetUp("", port_relay_, true/**bind to this port*/);
	BuildFDMap();
}

void Tun::InformServerAddr(int sock_fd, const sockaddr_in *server_addr)
{
	//modified by Zeng
	char buffer[PKT_SIZE] = {0};
	snprintf(buffer, PKT_SIZE, "connect\0");
	strcpy(buffer + strlen(buffer), client_ip_tun_);
	//end modification
	if (sendto(sock_fd, buffer, strlen(buffer)+1, 0, (struct sockaddr*)server_addr, sizeof(sockaddr_in)) == -1) 
		perror("InformServerAddr: UDP sendto fails!");
	if (recvfrom(sock_fd, buffer, PKT_SIZE, 0, NULL, NULL) == -1) 
		perror("InformServerAddr: Recvfrom fails!");
	if (strcmp(buffer, "accept"))
	{
		printf("Invalid reply msg[%s]\n", buffer);
		exit(0);
	}
	//printf("Server ethernet ip: %s\n", inet_ntoa(server_addr->sin_addr));
}

uint16_t Tun::Read(const IOType &type, char *buf, uint16_t len)
{
	uint16_t nread=-1;
	assert(len > 0);

	if (type == kTun)
	{
		nread = cread(tun_fd_, buf, len);
	}
	else if (type == kBackWspace)
	{
		nread = recvfrom(sock_fd_ath_, buf, len, 0, NULL, NULL);
	}
	else if (type == kFrontWspace)  /** For front relay over Ethernet.*/
	{
		nread = relay_sock_.RecvFrom(buf, len);
	}
	else if (type == kCellular)
	{
		nread = recvfrom(sock_fd_eth_, buf, len, 0, NULL, NULL);
	}
	else
		assert(0); 
	assert(nread > 0);
	return nread;
}

uint16_t Tun::Read(const vector<IOType> &type_arr, char *buf, uint16_t len, IOType *type_out)
{
	uint16_t nread = 0;
	int max_fd = -1;
	fd_set rd_set;
	FD_ZERO(&rd_set);

	for (size_t i = 0; i < type_arr.size(); i++)
	{
		int fd = fd_map_[type_arr[i]];
		FD_SET(fd, &rd_set);
		max_fd = max(max_fd, fd);
	}

	select(max_fd+1, &rd_set, NULL, NULL, NULL);

	/** Check which interface has the packet and read it.*/
	for (size_t i = 0; i < type_arr.size(); i++)
	{
		IOType IO_type = type_arr[i];
		int fd = fd_map_[IO_type];
		if (FD_ISSET(fd, &rd_set))
		{
			nread = Read(IO_type, buf, len);
			*type_out = IO_type;
			break;
		}
	}
	assert(nread > 0);  /** We must have read sth from some interface.*/
	return nread;
}

uint16_t Tun::Write(const IOType &type, char *buf, uint16_t len)
{
	uint16_t nwrite=-1;
	assert(len > 0);

	if (type == kTun)
	{
		nwrite = cwrite(tun_fd_, buf, len);
	}
	else if (type == kCellular)
	{  
		nwrite = sendto(sock_fd_eth_, buf, len, 0, (struct sockaddr*)&server_addr_eth_, sizeof(server_addr_eth_));
	}
	else
		assert(0);
	assert(nwrite == len);
	return nwrite;
}

void Tun::BuildFDMap()
{
	IOType type_arr[] = {kTun, kFrontWspace, kBackWspace, kCellular};
	int sz = sizeof(type_arr)/sizeof(type_arr[0]);
	for (int i = 0; i < sz; i++)
	{
		IOType IO_type = type_arr[i];
		fd_map_[IO_type] = GetFD(IO_type);
	}
}

int Tun::GetFD(const IOType &type)
{
	if (type == kTun)
		return tun_fd_;
	else if (type == kBackWspace)
		return sock_fd_ath_;
	else if (type == kFrontWspace)
		return relay_sock_.sid();
	else if (type == kCellular)
		return sock_fd_eth_;
	else
		assert(0);
}

inline int cread(int fd, char *buf, int n)
{
	int nread;

	if((nread=read(fd, buf, n)) < 0)
	{
		perror("Reading data");
	}
	return nread;
}

inline int cwrite(int fd, char *buf, int n)
{
	int nwrite;

	if((nwrite=write(fd, buf, n)) < 0)
	{
		perror("Writing data");
	}
	return nwrite;
}
