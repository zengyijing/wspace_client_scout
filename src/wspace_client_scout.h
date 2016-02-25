#ifndef WSPACE_ASYM_CLIENT_H_
#define WSPACE_ASYM_CLIENT_H_

#include <algorithm>

#include "wspace_asym_util.h"
#include "tun.h"
#include "fec.h"
#include "gps_parser.h"

#define PKT_QUEUE_SIZE 500

class OriginalSeqContext {
 public:
  OriginalSeqContext(): max_seq_(0) { Pthread_mutex_init(&lock_, NULL); }
  ~OriginalSeqContext() { Pthread_mutex_destroy(&lock_); }
  
  bool need_update(uint32 cmp_seq);
  void set_max_seq(uint32 seq);
  
 private:
  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }

  uint32 max_seq_;
  pthread_mutex_t lock_;
};

class RadioContext {
 public:
  RadioContext(int bs_id): decoder_(CodeInfo::kDecoder, MAX_BATCH_SIZE, PKT_SIZE),
                           bs_id_(bs_id), cellular_pkt_buf_(PKT_QUEUE_SIZE) {}
  ~RadioContext() {}

  RxRawBuf* raw_pkt_buf() { return &raw_pkt_buf_; }
  RxDataBuf* data_pkt_buf() { return &data_pkt_buf_; }
  PktQueue* cellular_pkt_buf() { return &cellular_pkt_buf_; }
  BatchInfo* batch_info() { return &batch_info_; }
  CodeInfo* decoder() { return &decoder_; }

  pthread_t* p_rx_rcv_ath() { return &p_rx_rcv_ath_; }
  pthread_t* p_rx_write_tun() { return &p_rx_write_tun_; }
  pthread_t* p_rx_create_data_ack() { return &p_rx_create_data_ack_; }
  pthread_t* p_rx_create_raw_ack() { return &p_rx_create_raw_ack_; }
  int bs_id() { return bs_id_; }
  void set_bs_id(int bs_id) { bs_id_ = bs_id; }

 private:
  int bs_id_;
  RxRawBuf  raw_pkt_buf_;
  RxDataBuf data_pkt_buf_;
  PktQueue  cellular_pkt_buf_; 
  CodeInfo  decoder_;
  BatchInfo batch_info_;
  pthread_t p_rx_rcv_ath_, p_rx_write_tun_, p_rx_create_data_ack_, p_rx_create_raw_ack_; 
};

// Store information about the current base station serving this client.
class BaseStationContext {
 public:
  BaseStationContext(): bs_id_(-1) { Pthread_mutex_init(&lock_, NULL); }
  ~BaseStationContext() { Pthread_mutex_destroy(&lock_); }
  
  int bs_id();
  void set_bs_id(int bs_id);
  
 private:
  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }

  int bs_id_;
  pthread_mutex_t lock_;
};

class WspaceClient {
 public:
  WspaceClient(int argc, char *argv[], const char *optstring);
  ~WspaceClient();
  
  void* RxRcvAth(void* arg);

  void* RxRcvCell(void* arg);

  void* RxWriteTun(void* arg);

  void* RxCreateDataAck(void* arg);

  void* RxCreateRawAck(void* arg);

  void* RxSendCell(void* arg);

  void* RxParseGPS(void* arg);

  void ParseIP(const vector<int> &ids, map<int, string> &ip_table, map<int, struct sockaddr_in> &addr_table);
  void Init();

// Data member
  pthread_t  p_rx_send_cell_, p_rx_rcv_cell_, p_rx_parse_gps_;
  Tun tun_;
  int ack_time_out_;  /** in ms. */
  int block_time_;    /** in ms. */
  uint8 max_ack_cnt_;
  GPSParser gps_parser_;
  int min_pkt_cnt_;  /** Wait for some packets to send the raw ack. */
  vector<int> bs_ids_;
  OriginalSeqContext original_seq_context_;
  map<int, RadioContext*> radio_context_tbl_;
  BaseStationContext bs_context_;
  string f_gps_;

 private:
  /**
   * Receive packets from the base station, from the front laptop, 
   * and from the cellular.
   */
  void RcvDownlinkPkt(char *pkt, uint16 *len, int radio_id);

  template<class T>
  friend bool IsIndExist(T *arr, int len, T val);
};

// Wrapper function for pthread_create
void* LaunchRxRcvAth(void* arg);
void* LaunchRxRcvCell(void* arg);
void* LaunchRxWriteTun(void* arg);
void* LaunchRxCreateDataAck(void* arg);
void* LaunchRxCreateRawAck(void* arg);
void* LaunchRxSendCell(void* arg);
void* LaunchRxParseGPS(void* arg);

#endif
