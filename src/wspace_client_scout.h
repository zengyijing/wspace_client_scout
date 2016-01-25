#ifndef WSPACE_ASYM_CLIENT_H_
#define WSPACE_ASYM_CLIENT_H_

#include <algorithm>

#include "wspace_asym_util.h"
#include "tun.h"
#include "fec.h"
#include "gps_parser.h"

class OriginalSeqContext {
 public:
  OriginalSeqContext();
  ~OriginalSeqContext();
  
  uint32 max_seq();
  void set_max_seq(uint32 seq);
  
 private:
  void Lock();
  void UnLock();
  uint32 max_seq_;
  pthread_mutex_lock lock_;
};

class RadioContext {
 private:
  RxRawBuf raw_pkt_buf_;
  RxDataBuf data_pkt_buf_;
  CodeInfo decoder_;
  BatchInfo batch_info_;
  pthread_t p_rx_rcv_ath_, p_rx_write_tun_, p_rx_create_data_ack_, p_rx_create_raw_ack_tbl_; 
};
// @yijing: m[radio_id] = RadioContext(1, 2, 3);

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
  void Init();

// Data member
  RxDataBuf  data_pkt_buf_;  /** Store the data packets and the data sequence number for retransmission. */
  pthread_t p_rx_rcv_ath_, p_rx_write_tun_, p_rx_create_data_ack_, p_rx_send_cell_, p_rx_parse_gps_;
  map<int, pthread_t> p_rx_create_raw_ack_tbl_; // <radio_id, p_rx_create_raw_ack_>.
  Tun tun_;
  int ack_time_out_;  /** in ms. */
  int block_time_;    /** in ms. */
  uint8 max_ack_cnt_;
  map<int, CodeInfo*> decoder_tbl_; // <radio_id, decoder_*> 
  map<int, RxRawBuf> raw_pkt_buf_tbl_; // <radio_id, raw_pkt_buf_>.
  BatchInfo batch_info_;  /** Pass the info between RxRcvAth and CreateDataAck. */
  GPSParser gps_parser_;
  int min_pkt_cnt_;  /** Wait for some packets to send the raw ack. */
  vector<int> bs_ids_;
  OrignalSeqContext original_seq_context_;
  
 private:
  /**
   * Receive packets from the base station, from the front laptop, 
   * and from the cellular.
   */
  void RcvDownlinkPkt(char *pkt, uint16 *len, Tun::IOType *type_out, int *radio_id);

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
