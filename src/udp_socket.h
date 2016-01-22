#ifndef UDP_SOCKET_
#define UDP_SOCKET_

#include "c_lib.h"
#include "cpp_lib.h"

class UdpSocket {
 private:
  int32_t sid_;
  struct sockaddr_in addr_;

 public:
  UdpSocket();
  ~UdpSocket();
//====================================================================================================
  /**
  * This UDP socket bind to a ip address (usually itself or a broadcast address) and port number
  * The ip should be a broadcast ip to receive broadcast message
  * @param [in] ip: the ip address to store into the addr 
  * @param [in] port: the port number 
  * @param [in] is_recv: the socket is for receiving or transmitting.
  */
  void SocketSetUp(const string& ip, const int32_t& port, bool is_recv);

//====================================================================================================
  /**
  * Once the socket bind to a ip address, it is able to send packet to any other UDP socket
  * with ip address and port number
  * @param [in] send_data: the data send to the destination
  * @param [in] send_len: the length of the data to be sent.
  * @param return: the length of the data actually sent. 
  */ 
   int32_t SendTo(const char* send_data, int32_t send_len);

//====================================================================================================
  /**
  * Once the socket bind to a ip address and a port number, it is able to hear that port
  * @param [in] data: the buffer address where the received packet should be stored.
  * @param [in] read_len: the packet length to read.
  * @return the number of bytes received from the source. 
  */ 
  int32_t RecvFrom(char* data, int32_t read_len);

  int32_t sid() const { return sid_; }
};

#endif
