#include "udp_socket.h"


UdpSocket::UdpSocket()
{
	sid_ = socket(AF_INET, SOCK_DGRAM, 0);
	assert (sid_ != -1);  
}

UdpSocket::~UdpSocket()
{
	close(sid_);
}
  
void UdpSocket::SocketSetUp(const string& ip, const int32_t& port, bool is_recv)
{
	/*UDP server part*/
	addr_.sin_family = AF_INET;
	addr_.sin_port = htons(port);
	bzero(&(addr_.sin_zero),8);
	if (!is_recv)
		addr_.sin_addr.s_addr = inet_addr(ip.c_str());
	else
	{
		addr_.sin_addr.s_addr = htonl(INADDR_ANY);
		int32_t res_bind = bind(sid_, (struct sockaddr *)&addr_, sizeof(struct sockaddr));
		if(-1==res_bind)
		{
			perror("Error: UdpSocket bind failed in UdpSocketSetUp()");
		}

		int32_t multi_use = 1;
		int32_t check_mu = setsockopt(sid_,  SOL_SOCKET,  SO_REUSEADDR,  &multi_use,  sizeof(multi_use));  
		if(-1 == check_mu)
		{
			perror("Error: UdpSocket set multiple usage failed in UdpSocketSetUp()");
		}
	}
}

int32_t UdpSocket::SendTo(const char* send_data, int32_t send_len)
{
	int actual_send_len = sendto(sid_, send_data, send_len, 0, (struct sockaddr *)&addr_, sizeof(struct sockaddr)); 
	return actual_send_len;
}

int32_t UdpSocket::RecvFrom(char* data, int32_t recv_len)
{
	int32_t bytes_read = recvfrom(sid_, data, recv_len, 0, NULL, NULL);
	return bytes_read;  
}
