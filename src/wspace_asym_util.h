#ifndef WSPACE_ASYM_UTIL_H_
#define WSPACE_ASYM_UTIL_H_ 

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <deque>
#include <vector>
#include <string>

#include "pthread_wrapper.h"
#include "time_util.h"

/* Parameter to be tuned */
#define BUF_SIZE 5000
#define PKT_SIZE 1472
#define ACK_WINDOW 500
#define MAX_BATCH_SIZE 10
#define MAX_ACK_CNT 1  /** Number of times the sequence number of each lost packet is sent over cellular.*/
/* end parameter to be tuned*/

#define ATH_DATA 1
#define ATH_CODE 2
#define DATA_ACK 3
#define RAW_ACK 5
#define CELL_DATA 6
#define GPS 7
#define BS_STATS 8
#define CONTROLLER_TO_CLIENT 9
#define CLIENT_TO_CONTROLLER 10

#define INVALID_SEQ_NUM 0

#define ATH_DATA_HEADER_SIZE   (uint16) sizeof(AthDataHeader)
#define ATH_CODE_HEADER_SIZE   (uint16)sizeof(AthCodeHeader) 
#define CELL_DATA_HEADER_SIZE  (uint16) sizeof(CellDataHeader)
#define ACK_HEADER_SIZE      (uint16) sizeof(AckHeader)
#define GPS_HEADER_SIZE      (uint16) sizeof(GPSHeader)
#define ACK_PKT_SIZE           (uint16) sizeof(AckPkt)

/* B */
#define ATH5K_RATE_CODE_1M  0x1B
#define ATH5K_RATE_CODE_2M  0x1A
#define ATH5K_RATE_CODE_5_5M  0x19
#define ATH5K_RATE_CODE_11M  0x18
/* A and G */
#define ATH5K_RATE_CODE_6M  0x0B
#define ATH5K_RATE_CODE_9M  0x0F
#define ATH5K_RATE_CODE_12M  0x0A
#define ATH5K_RATE_CODE_18M  0x0E
#define ATH5K_RATE_CODE_24M  0x09
#define ATH5K_RATE_CODE_36M  0x0D
#define ATH5K_RATE_CODE_48M  0x08
#define ATH5K_RATE_CODE_54M  0x0C

/** Time slot for ath5k*/
#define DIFS_80211b 28
#define DIFS_80211ag 28
#define SLOT_TIME 9

//#define TEST
//#define RAND_DROP
#define WRT_DEBUG

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;

enum Status {
  kEmpty = 1, /** The slot is empty for storing packets. */
  kOccupiedNew = 2, /** The slot stores a new packet. */
  kBlock = 5, /**rx_write_tun() is block waiting for this packet. */
  kDrop = 6,  /** This packet is given up waiting for. */
  kRead = 7,  /** Indicate the packet has been read. */
};

enum LockOwner {
  kFree = 0,
  kRcvAth = 1,
  kWriteTun = 2,
  kCreateAck = 3,
  kSendCell = 4,
};
  
typedef struct {
  uint32 seq_num;
  Status status;
  uint16 len;    // Length of shim layer header + data
  uint8  num_retrans;
  TIME timestamp;
  LockOwner owner;
} BookKeeping;

void Perror(char *msg, ...);

class BasicBuf {
 public:
  BasicBuf(): kSize(BUF_SIZE), head_pt_(0), tail_pt_(0) {
    // Clear bookkeeping
    for (int i = 0; i < kSize; i++) {
      book_keep_arr_[i].seq_num = 0;
      book_keep_arr_[i].status = kEmpty;
      book_keep_arr_[i].len = 0;
      book_keep_arr_[i].num_retrans = 0;
      book_keep_arr_[i].timestamp.GetCurrTime();
    }
    // clear packet buffer
    bzero(pkt_buf_, sizeof(pkt_buf_));
    // initialize queue lock 
    Pthread_mutex_init(&qlock_, NULL);
    // initialize lock array
    for (int i = 0; i < kSize; i++) {
      Pthread_mutex_init(&(lock_arr_[i]), NULL);
    }
    // initialize cond variables
    Pthread_cond_init(&empty_cond_, NULL);
    Pthread_cond_init(&fill_cond_, NULL);
  }  

  ~BasicBuf() {
    // Reset pointer
    head_pt_ = 0;
    tail_pt_ = 0;
    // Destroy lock
    Pthread_mutex_destroy(&qlock_);
    for (int i = 0; i < kSize; i++) {
      Pthread_mutex_destroy(&(lock_arr_[i]));
    }
    // Destroy conditional variable
    Pthread_cond_destroy(&empty_cond_);
    Pthread_cond_destroy(&fill_cond_);
  }

  void LockQueue() {
    Pthread_mutex_lock(&qlock_);
  }

  void UnLockQueue() {
    Pthread_mutex_unlock(&qlock_);
  }

  void LockElement(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("LockElement invalid index: %d\n", index);
    }
    Pthread_mutex_lock(&lock_arr_[index]);
  }

  void UnLockElement(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("UnLockElement invalid index: %d\n", index);
    }
    book_keep_arr_[index].owner = kFree;
    Pthread_mutex_unlock(&lock_arr_[index]);
  }

  void LockElement(uint32 index, LockOwner owner) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("LockElement invalid index: %d\n", index);
    }
    Pthread_mutex_lock(&lock_arr_[index]);
    book_keep_arr_[index].owner = owner;
  }

  LockOwner ElementLockStatus(uint32 index) {
    return book_keep_arr_[index].owner;
  }

  void WaitFill() {
    Pthread_cond_wait(&fill_cond_, &qlock_);
  }  

  void WaitEmpty() {
    Pthread_cond_wait(&empty_cond_, &qlock_);
  }

  void SignalFill() {
    Pthread_cond_signal(&fill_cond_);
  }

  void SignalEmpty() {
    Pthread_cond_signal(&empty_cond_);
  }

  bool IsFull() {
    return ((head_pt_ + BUF_SIZE) == tail_pt_);
  }

  bool IsEmpty() {
    return (tail_pt_ == head_pt_);
  }

  uint32 head_pt() const { return head_pt_; }

  uint32 head_pt_mod() const { return (head_pt_%kSize); }

  void set_head_pt(uint32 head_pt) { head_pt_ = head_pt; }

  uint32 tail_pt() const { return tail_pt_; }

  uint32 tail_pt_mod() const { return (tail_pt_%kSize); }

  void set_tail_pt(uint32 tail_pt) { tail_pt_ = tail_pt; }

  void IncrementHeadPt() {
    head_pt_++;
  }

  void IncrementTailPt() {
    tail_pt_++;
  }
  
  void GetPktBufAddr(uint32 index, char **pt) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetPktBufAddr invalid index: %d\n", index);
    }
    *pt = (char*)pkt_buf_[index];
  }

  void StorePkt(uint32 index, uint16 len, const char* pkt) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("StorePkt invalid index: %d\n", index);
    }
    memcpy((void*)pkt_buf_[index], pkt, (size_t)len);
  }

  void GetPkt(uint32 index, uint16 len, char* pkt) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetPkt invalid index: %d\n", index);
    }
    memcpy(pkt, (void*)pkt_buf_[index], (size_t)len);
  }
    
  void AcquireHeadLock(uint32 *index);    // Function acquires and releases qlock_ inside

  void AcquireTailLock(uint32 *index);    // Function acquires and releases qlock_ inside

  void UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len);

  void GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len) const;

  void UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len, uint8 num_retrans, bool update_timestamp);

  void GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len, uint8 *num_retrans,
            TIME *timestamp) const;


  Status& GetElementStatus(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetElementStatus invalid index: %d\n", index);
    }
    return book_keep_arr_[index].status;
  }

  uint32& GetElementSeqNum(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetElementSeqNm invalid index: %d\n", index);
    }
    return book_keep_arr_[index].seq_num;
  }

  uint16& GetElementLen(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetElementLen invalid index: %d\n", index);
    }
    return book_keep_arr_[index].len;
  }

  uint8& GetElementNumRetrans(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetElementRetrans invalid index: %d\n", index);
    }
    return book_keep_arr_[index].num_retrans;
  }

  TIME& GetElementTimeStamp(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("GetElementTimeStamp invalid index: %d\n", index);
    }
    return book_keep_arr_[index].timestamp;
  }

// Data member
  const uint32 kSize;
  uint32 head_pt_;
  uint32 tail_pt_;  
  BookKeeping book_keep_arr_[BUF_SIZE];
  char pkt_buf_[BUF_SIZE][PKT_SIZE];
  pthread_mutex_t qlock_;
  pthread_mutex_t lock_arr_[BUF_SIZE];
  pthread_cond_t empty_cond_, fill_cond_;
};

class TxSendBuf: public BasicBuf {
 public:
  TxSendBuf(): curr_pt_(0) {
  }  

  ~TxSendBuf() {
    curr_pt_ = 0;
  }

  void AcquireCurrLock(uint32 *index);

  uint32 curr_pt() const { return curr_pt_; }

  uint32 curr_pt_mod() const { return (curr_pt_%kSize); }

  void set_curr_pt(uint32 curr_pt) { curr_pt_ = curr_pt; }

  void IncrementCurrPt() {
    curr_pt_++;
  }

  bool IsEmpty() {
    return (tail_pt_ == curr_pt_);
  }

// Data member
  uint32 curr_pt_;
};

/**
 * Pass parameters between RxRcvAth and GenerateDataAck.
 */
class BatchInfo {
 public:
  BatchInfo();
  ~BatchInfo();

  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }

  /**
   * Note: locking is included.
   */
  void SetBatchInfo(uint32 batch_id, uint32 seq, bool decoding_done, int ind, int n, uint32 pkt_duration);

  /**
   * Note: locking is included.
   */
  void GetBatchInfo(uint32 *seq, bool *decoding_done);

  /**
   * Note: locking is included.
   */
  void set_decoding_done(bool decoding_done);

  bool decoding_done(bool is_lock=true);

  /**
   * Note: locking is included.
   */
  void GetBatchID(uint32 *batch_id);

  /**
   * Update the time left for receiving all the packets. Update at per packet
   * basis.
   * @param [in] ind: the current received packet.
   * @param [in] n: the total number of encoded packets to be received.
   * @param [in] pkt_duration: transmission duration of each packet in us.
   * @param [in] lock or not.
   */
  void UpdateTimeLeft(int ind, int n, uint32 pkt_duration, bool is_lock=true);

  bool IsTimeOut();

 private:
  uint32 batch_id_; 
  uint32 highest_decoded_seq_;  /** Highest sequence number in the previous batch.*/
  bool decoding_done_;
  TIME cur_recv_time_;   /** The time to receive the current packet.*/
  double time_left_;     /** Duration left to finish receiving the current batch(in us). */
  pthread_mutex_t lock_;
};

class RxDataBuf: public BasicBuf {
 public:
  RxDataBuf() : highest_decoded_seq_(INVALID_SEQ_NUM) {
    Pthread_cond_init(&element_avail_cond_, NULL);
    Pthread_cond_init(&wake_ack_cond_, NULL);
  }

  ~RxDataBuf() {
    Pthread_cond_destroy(&element_avail_cond_);
    Pthread_cond_destroy(&wake_ack_cond_);
  }

  void SignalElementAvail() {
    Pthread_cond_signal(&element_avail_cond_);
  }

  void WaitElementAvail(uint32 index) {
    if (index < 0 || index > BUF_SIZE-1) {
      Perror("WaitElementAvail invalid index: %d\n", index);
    }
    Pthread_cond_wait(&element_avail_cond_, &lock_arr_[index]); 
  }

  void SignalWakeAck() {
    Pthread_cond_signal(&wake_ack_cond_);
  }

  int WaitWakeAck(int wait_s) {
    static struct timespec time_to_wait = {0, 0};
    struct timeval now;
    gettimeofday(&now, NULL);
    time_to_wait.tv_sec = now.tv_sec+wait_s;
    //time_to_wait.tv_nsec = (now.tv_usec + wait_ms * 1000) * 1000;
    int err = pthread_cond_timedwait(&wake_ack_cond_, &qlock_, &time_to_wait);
    return err;
  }

  void AcquireHeadLock(uint32 *index, uint32 *head);

  void EnqueuePkt(uint32 seq_num, uint16 pkt_len, const char *pkt);

  /**
   * Copy the current packet into the buffer.
   * @param [in] len: the length of the packet.
   * @param [in] pkt: the buffer address to store the dequeued packet. Have to copy because I'll
   * unlock the queue.
   * @return true - the packet at the current slot is successfully dequeued. false - 
   * the packet is given up.
   */
  bool DequeuePkt(uint16 *len, uint8 *pkt);

  /**
   * Return the sequence numbers of the lost packets which have blocked for 
   * less than block_time.
   * Note: Locking is included.
   * @param [in] ack_time_out: timeout value to generate ACK. If the buffer is empty,
   * @param [in] max_num_nacks: the max number of nacks to be inserted.
   * @param [in] batch_info: information about current decoded batch to determine the end seq.
   * @param [out] nack_seq_arr: Array of sequence numbers to be nacked.
   * @param [out] end_seq: The sequence number of the last good packet.
   */
  void FindNackSeqNum(int block_time, int max_num_nacks, BatchInfo &batch_info, 
        std::vector<uint32> &nack_seq_arr, uint32 &end_seq);

  void set_highest_decoded_seq(uint32 seq); 

  uint32 highest_decoded_seq() const { return highest_decoded_seq_; }

// Data member
  pthread_cond_t element_avail_cond_;  // Associate with element locks
  pthread_cond_t wake_ack_cond_;       // Associate with qlock
  uint32 highest_decoded_seq_;         // The highest sequence number of the most recent decoded batch.
};

// Four packet headers
class AthHeader {
 public:
  AthHeader() {}
  ~AthHeader() {}
  AthHeader(char type, uint16 rate) : type_(type), raw_seq_(0), rate_(rate) {}

  uint16 GetRate();
  void SetRate(uint16 rate);

  void IncrementRawSeq() { raw_seq_++; }
  uint32 raw_seq() const { return raw_seq_; }

#ifdef RAND_DROP
  bool is_good() const { return is_good_; }
  void set_is_good(bool is_good) { is_good_ = is_good; } 
#endif

// Data
  char type_;
  uint32 raw_seq_;
  uint16 rate_;
#ifdef RAND_DROP
  bool is_good_;
#endif
};

class AthDataHeader : public AthHeader {
 public:
  AthDataHeader() : AthHeader(ATH_DATA, ATH5K_RATE_CODE_6M), seq_num_(0), len_(0) {}
  ~AthDataHeader() {}
  void SetAthHdr(uint32 seq_num, uint16 len, uint16 rate);
  void ParseAthHdr(uint32 *seq_num, uint16 *len, char *rate);

// Data
  uint32 seq_num_;
  uint16 len_;    // Length of the data
};

class AthCodeHeader : public AthHeader {
 public:
  AthCodeHeader() : AthHeader(ATH_CODE, ATH5K_RATE_CODE_6M), start_seq_(0), ind_(0), k_(0), n_(0) {}
  ~AthCodeHeader() {}
  void SetHeader(uint32 batch_id, uint32 start_seq, char type, int ind, int k, int n, const uint16 *len_arr);
  void SetInd(uint8 ind) { ind_ = ind; }
  void ParseHeader(uint32 *batch_id, uint32 *start_seq, int *ind, int *k, int *n) const;  
  /** Should copy the len_arr to somewhere. */
  uint16* lens() {
    assert(k_ > 0);
    return (uint16*)((uint8*)this + ATH_CODE_HEADER_SIZE);
  }
  uint8* GetPayloadStart() const {
    return ( (uint8*)this + ATH_CODE_HEADER_SIZE + k_ * sizeof(uint16) );
  }
  int GetFullHdrLen() const {
    return (ATH_CODE_HEADER_SIZE + k_ * sizeof(uint16));
  }

 private:
  uint32 batch_id_;   /** Distinguish packets from different batches - for retransmission. start from 1*/
  uint32 start_seq_;  /** indicate the sequence number of the first packet in this batch. */
  uint8 ind_;          /** current index of the coding packet. */
  uint8 k_;            /** number of data packets. */
  uint8 n_;            /** number of encoded packets. */
};
  
class CellDataHeader {
 public:
  CellDataHeader(): type_(CELL_DATA) {}
  ~CellDataHeader() {}

// Data
  char type_;
}; 

class ControllerToClientHeader {
 public:
  ControllerToClientHeader(): type_(CONTROLLER_TO_CLIENT) {}
  ~ControllerToClientHeader() {}

  void set_client_id (int id) { client_id_ = id; }
  int client_id() const { return client_id_; }
  char type() const { return type_; }

 private:
  char type_;
  int client_id_;
};

class AckHeader {
 public:
  AckHeader(): ack_seq_(0), num_nacks_(0), start_nack_seq_(0), end_seq_(0), num_pkts_(0) {}
  ~AckHeader() {}

  void Init(char type) {
    type_ = type;
    ack_seq_++;
    num_nacks_ = 0;
    start_nack_seq_ = 0;
    end_seq_ = 0;
    num_pkts_ = 0;
  }

  void set_end_seq(uint32 end_seq) { end_seq_ = end_seq; }

  uint16 num_nacks() const { return num_nacks_; }

  uint32 ack_seq() const { return ack_seq_; }

  uint16 num_pkts() const { return num_pkts_; }

  void set_num_pkts(uint16 num_pkts) { num_pkts_ = num_pkts; }

  void set_ids(int client_id, int radio_id) { client_id_ = client_id; radio_id_ = radio_id; }
// Data
  char type_;
  uint32 ack_seq_;          // Record the sequence number of ack 
  uint16 num_nacks_;        // number of nacks in the packet
  uint32 start_nack_seq_;   // Starting sequence number of nack
  uint32 end_seq_;          // The end of this ack window - could be a good pkt or a bad pkt 
  uint16 num_pkts_;         // Total number of packets included in this ack.
  int client_id_;
  int radio_id_;
}; 

class GPSHeader {
 public:
  GPSHeader() : type_(GPS), seq_(0), speed_(-1.0) {}
  ~GPSHeader() {}

  void Init(double time, double latitude, double longitude, double speed);

  uint32 seq() const { assert(seq_ > 0); return seq_; }

  double speed() const { assert(speed_ >= 0); return speed_; }

 private: 
  friend class GPSLogger;

  char type_; 
  uint32 seq_;  /** sequence number.*/
  double time_;
  double latitude_;
  double longitude_;
  double speed_; 
};

class GPSLogger {
 public:
  GPSLogger();
  ~GPSLogger();

  void ConfigFile(const char* filename = NULL);

  void LogGPSInfo(const GPSHeader &hdr);

  std::string filename() const { return filename_; }

 private:
  std::string filename_;
  FILE *fp_;
};

class AckPkt {
 public:
  AckPkt() { bzero(rel_seq_arr_, sizeof(rel_seq_arr_)); }
  ~AckPkt() {}

  void PushNack(uint32 seq);

  void ParseNack(char *type, uint32 *ack_seq, uint16 *num_nacks, uint32 *end_seq, int* client_id, int* radio_id, uint32 *seq_arr, uint16 *num_pkts=NULL);

  uint16 GetLen() {
    uint16 len = sizeof(ack_hdr_) + sizeof(rel_seq_arr_[0]) * ack_hdr_.num_nacks_;
    assert(len <= PKT_SIZE && len > 0);
    return len;
  }

  void Print();

  bool IsFull() { return ack_hdr_.num_nacks() >= ACK_WINDOW; }

  void Init(char type) { ack_hdr_.Init(type); }

  void set_end_seq(uint32 end_seq) { ack_hdr_.set_end_seq(end_seq); }

  uint32 ack_seq() const { return ack_hdr_.ack_seq(); }

  void set_num_pkts(uint16 num_pkts) { ack_hdr_.set_num_pkts(num_pkts); }

  void set_ids(int client_id, int radio_id) { ack_hdr_.set_ids(client_id, radio_id); }
 private:
  AckHeader& ack_hdr() { return ack_hdr_; }

  AckHeader ack_hdr_;
  uint16 rel_seq_arr_[ACK_WINDOW];
};

inline void AckPkt::PushNack(uint32 seq) {
  if (ack_hdr_.num_nacks_ == 0) {
    ack_hdr_.start_nack_seq_ = seq;
    rel_seq_arr_[0] = 0;  // Relative seq
  }
  else {
    assert(ack_hdr_.num_nacks_ < ACK_WINDOW);
    rel_seq_arr_[ack_hdr_.num_nacks_] = (uint16)(seq - ack_hdr_.start_nack_seq_);
  }
  ack_hdr_.num_nacks_++;
}

inline uint32 Seq2Ind(uint32 seq) {
  return ((seq-1) % BUF_SIZE);
}

/**
 * This class tracks the status of each raw packet
 * at the receiver side. 
 *
 */
class RawPktRcvStatus {
 public:
  RawPktRcvStatus() {}
  ~RawPktRcvStatus() {}
  RawPktRcvStatus(uint32 seq, uint8 send_cnt) : seq_(seq), send_cnt_(send_cnt) {}

  void Print() const {
    printf("seq[%u] send_cnt[%u]\n", seq_, send_cnt_);
  }

// Data member
  uint32 seq_;
  uint8  send_cnt_;    /** Number of times this ack has been sent as feedback. */
};

/** 
 * A bookkeeping structure to track the relevant info of each
 * raw packet at the receiver (Client).
 */
class RxRawBuf {
 public:
  RxRawBuf() : end_seq_(INVALID_SEQ_NUM), max_send_cnt_(1), pkt_cnt_(0), kMaxBufSize(ACK_WINDOW) { 
    assert(max_send_cnt_ > 0);
    nack_deq_.clear();
    Pthread_mutex_init(&lock_, NULL);
    Pthread_cond_init(&fill_cond_, NULL);
  }

  ~RxRawBuf() {
    Pthread_mutex_destroy(&lock_);
    Pthread_cond_destroy(&fill_cond_);
  }

  /**
   * Store this sequence as the highest sequence number of the good packet.
   * Push lost packets (holes) in the buffer for generating NACK.
   * Note: Locking is included.
   * @param [in] good_seq the sequence number of the current packet received.
   * @return true - insertion succeeds, false - out of order packets.
   */
  bool PushPkts(uint32 cur_seq, bool is_cur_good);
  
  /**
   * Return the sequence number of lost packets with send_cnt < max_send_cnt, 
   * pop out entries with send_cnt = max_send_cnt.
   * Note: Locking is included. 
   * @param [in] min_pkt_cnt: number of new packets in this raw ack packet.
   * @param [out] nack_seq_vec: Vector of sequence numbers of loss packets for NACK.
   * @param [out] good_seq: Highest sequence number of the good packet received. 
   */
  void PopPktStatus(std::vector<uint32> &seq_vec, uint32 *good_seq, uint16 *num_pkts, uint32 min_pkt_cnt = 10);

  /**
   * Print out the status of each raw packet.
   * Note: Locking is included.
   */
  void Print();

  void set_max_send_cnt(uint8 max_send_cnt)  { max_send_cnt_ = max_send_cnt; }

  bool IsEmpty() const { return (pkt_cnt_ == 0); }

 private:
  void Lock() { Pthread_mutex_lock(&lock_); }
  void UnLock() { Pthread_mutex_unlock(&lock_); }
  void WaitFill() { Pthread_cond_wait(&fill_cond_, &lock_); }
  void SignalFill() { Pthread_cond_signal(&fill_cond_); }

  const int kMaxBufSize;
  std::deque<RawPktRcvStatus> nack_deq_; 
  uint32 end_seq_;          /** Highest sequence number of the good packet. */
  uint8  max_send_cnt_;      /** Maximum number of times for NACKing this sequence number. */
  uint32 pkt_cnt_;          /** Number of raw packets which have been curently logged. */
  pthread_mutex_t lock_;    /** Lock is needed because the bit map is access by two threads. */
  pthread_cond_t fill_cond_;  /** There are raw acks filled in. */
};

void PrintPkt(char *pkt, uint16 len);

#endif
