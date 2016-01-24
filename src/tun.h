#ifndef TUN_H_
#define TUN_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h> 
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <stdarg.h>

#include <map>
#include <algorithm>

#include "udp_socket.h"

/* buffer for reading from tun/tap interface, must be >= 1500 */
#define PKT_SIZE 2000   
#define PORT_ETH 55554
#define PORT_ATH 55555
#define PORT_RELAY 55556

class Tun {
 public:
  enum IOType {
    kTun=1,
    kFrontWspace,
    kBackWspace, 
    kCellular,
    kController, 
  };

  Tun(): tun_type_(IFF_TUN), port_eth_(PORT_ETH), 
         port_ath_(PORT_ATH), port_relay_(PORT_RELAY)  {
    if_name_[0] = '\0';
    controller_ip_eth_[0] = '\0';
    client_id_ = 0;
  }

  ~Tun() {
    close(tun_fd_);
    close(sock_fd_eth_);
    close(sock_fd_ath_);
  }
  
  void CreateConn();

  void InitSock();

  void InformServerAddr(int sock_fd, const sockaddr_in *server_addr);

  int AllocTun(char *dev, int flags);

  int CreateSock();

  void CreateAddr(const char *ip, int port, sockaddr_in *addr);
  void CreateAddr(int port, sockaddr_in *addr);

  void BindSocket(int fd, sockaddr_in *addr);

  void BuildFDMap();

  int GetFD(const IOType &type);
  
  uint16_t Read(const IOType &type, char *buf, uint16_t len);

  /**
   * Read from multiple interfaces.
   */
  uint16_t Read(const std::vector<IOType> &type_arr, char *buf, uint16_t len, IOType *type_out);

  uint16_t Write(const IOType &type, char *buf, uint16_t len, int bs_id);

// Data members:
  int tun_fd_;
  int tun_type_;        // TUN or TAP
  char if_name_[IFNAMSIZ];
  char controller_ip_eth_[16];
  int client_id_;
  std::map<int, string> bs_ip_tbl_; // <bs_id, bs_ip_eth_>.
  std::map<int, struct sockaddr_in> bs_addr_tbl_; // <bs_id, bs_addr>.
  struct sockaddr_in client_addr_eth_, client_addr_ath_, controller_addr_eth_; 
  uint16_t port_eth_, port_ath_, port_relay_;
  int sock_fd_eth_, sock_fd_ath_;       // Sockets to handle request at the server side
  UdpSocket relay_sock_;
  std::map<IOType, int> fd_map_;
};

int cread(int fd, char *buf, int n);
int cwrite(int fd, char *buf, int n);
int read_n(int fd, char *buf, int n);

#endif
