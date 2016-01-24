#ifndef WSPACE_ASYM_CLIENT_H_
#define WSPACE_ASYM_CLIENT_H_

#include <algorithm>

#include "wspace_asym_util.h"
#include "tun.h"
#include "fec.h"
#include "gps_parser.h"

class WspaceClient {
 public:
  WspaceClient(int argc, char *argv[], const char *optstring);
  ~WspaceClient() {}
  
  void* RxRcvAth(void* arg);

  void* RxWriteTun(void* arg);

  void* RxCreateDataAck(void* arg);

  void* RxCreateRawAck(void* arg);

  void* RxSendCell(void* arg);

  void* RxParseGPS(void* arg);

  void ParseIP(const vector<int> &ids, map<int, string> &ip_table, map<int, struct sockaddr_in> &addr_table);

// Data member
  RxDataBuf  data_pkt_buf_;  /** Store the data packets and the data sequence number for retransmission. */
  pthread_t p_rx_rcv_ath_, p_rx_write_tun_, p_rx_create_data_ack_, p_rx_send_cell_, p_rx_parse_gps_;
  map<int, pthread_t> p_rx_create_raw_ack_tbl_;
  Tun tun_;
  int ack_time_out_;  /** in ms. */
  int block_time_;    /** in ms. */
  uint8 max_ack_cnt_;
  CodeInfo decoder_; 
  map<int, RxRawBuf> raw_pkt_buf_tbl_; // <radio_id, RxRawBuf>
  BatchInfo batch_info_;  /** Pass the info between RxRcvAth and CreateDataAck. */
  GPSParser gps_parser_;
  int min_pkt_cnt_;  /** Wait for some packets to send the raw ack. */
  vector<int> bs_ids_;
 private:
  /**
   * Receive packets from the base station, from the front laptop, 
   * and from the cellular.
   */
  void RcvDownlinkPkt(char *pkt, uint16 *len, int *radio_id);

  template<class T>
  friend bool IsIndExist(T *arr, int len, T val);
};

// Wrapper function for pthread_create
void* LaunchRxRcvAth(void* arg);
void* LaunchRxWriteTun(void* arg);
void* LaunchRxCreateDataAck(void* arg);
void* LaunchRxCreateRawAck(void* arg);
void* LaunchRxSendCell(void* arg);
void* LaunchRxParseGPS(void* arg);

#endif
