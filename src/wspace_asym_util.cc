#include "wspace_asym_util.h"

using namespace std;
// Member functions for BasicBuf
/*
Purpose: Dequeue an element in the head
*/
void BasicBuf::AcquireHeadLock(uint32 *index) {
  LockQueue();
  while (IsEmpty()) {
#ifdef TEST
    printf("Empty!\n");
#endif
    WaitFill();
  }    
  *index = head_pt_mod(); //current element
  LockElement(*index);
  IncrementHeadPt();
  SignalEmpty();
  UnLockQueue();
}

/*
Purpose: Enqueue an element in the tail
*/
void BasicBuf::AcquireTailLock(uint32 *index) {
  LockQueue();
  while (IsFull()) {
//#ifdef TEST
    printf("Full!\n");
//#endif
    WaitEmpty();
  }    
  *index = tail_pt_mod(); //current element
  LockElement(*index);
  IncrementTailPt();
  SignalFill();
  UnLockQueue();
}

void BasicBuf::UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len) {
  if (index < 0 || index > BUF_SIZE-1) {
    Perror("UpdateBookKeeping invalid index: %d\n", index);
  }
  book_keep_arr_[index].seq_num = seq_num;
  book_keep_arr_[index].status = status;
  book_keep_arr_[index].len = len;
}

void BasicBuf::GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len) const {
  if (index < 0 || index > BUF_SIZE-1) {
    Perror("GetBookKeeping invalid index: %d\n", index);
  }
  *seq_num = book_keep_arr_[index].seq_num; 
  *status = book_keep_arr_[index].status;
  *len = book_keep_arr_[index].len; 
}

void BasicBuf::UpdateBookKeeping(uint32 index, uint32 seq_num, Status status, uint16 len, 
          uint8 num_retrans, bool update_timestamp) {
  if (index < 0 || index > BUF_SIZE-1) {
    Perror("UpdateBookKeeping invalid index: %d\n", index);
  }
  book_keep_arr_[index].seq_num = seq_num;
  book_keep_arr_[index].status = status;
  book_keep_arr_[index].len = len;
  book_keep_arr_[index].num_retrans = num_retrans;
  if (update_timestamp) {
    book_keep_arr_[index].timestamp.GetCurrTime();
  }
}

void BasicBuf::GetBookKeeping(uint32 index, uint32 *seq_num, Status *status, uint16 *len, 
        uint8 *num_retrans, TIME *timestamp) const {
  if (index < 0 || index > BUF_SIZE-1) {
    Perror("GetBookKeeping invalid index: %d\n", index);
  }
  *seq_num = book_keep_arr_[index].seq_num; 
  *status = book_keep_arr_[index].status;
  *len = book_keep_arr_[index].len; 
  *num_retrans = book_keep_arr_[index].num_retrans; 
  if (timestamp) {
    *timestamp = book_keep_arr_[index].timestamp; 
  }
}

// tx_send_buf
void TxSendBuf::AcquireCurrLock(uint32 *index) {
  LockQueue();
  while (IsEmpty()) {
#ifdef TEST
    printf("Empty!\n");
#endif
    WaitFill();
  }    
  *index = curr_pt_mod(); //current element
  LockElement(*index);
  IncrementCurrPt();
  UnLockQueue();
}

void RxDataBuf::AcquireHeadLock(uint32 *index, uint32 *head) {
  LockQueue();
  while (IsEmpty()) {
//#ifdef TEST
    printf("Empty!\n");
//#endif
    WaitFill();
  }    
  *index = head_pt_mod(); 
  *head = head_pt();
  LockElement(*index, kWriteTun);
  UnLockQueue();
}

void RxDataBuf::EnqueuePkt(uint32 seq_num, uint16 pkt_len, const char *pkt) {
  uint32 head_pt, tail_pt, index;
  uint32 rcv_ath_cnt=0;
  bool is_wrap = false;
  char *buf_addr=NULL;

  LockQueue();
  while (IsFull()) {
    //printf("rcv_buf Full!\n");
    LockElement(head_pt_mod(), kRcvAth);
    if (GetElementStatus(head_pt_mod()) == kBlock) {
      printf("Full Drop pkt[%u]\n", GetElementSeqNum(head_pt_mod()));
      GetElementStatus(head_pt_mod()) = kDrop;
      SignalElementAvail();
    }
    else {
      printf("Full deadlock! stat[%u] seq[%u]\n", 
        GetElementStatus(head_pt_mod()), GetElementSeqNum(head_pt_mod()));
      SignalElementAvail();
    }
    UnLockElement(head_pt_mod());
    WaitEmpty();
  }    

  head_pt = RxDataBuf::head_pt();
  tail_pt = RxDataBuf::tail_pt(); 

  if (seq_num > head_pt) {
    //rcv_ath_cnt++;
    //printf("rcv_ath_cnt: %u receive:[%u]\n", rcv_ath_cnt, seq_num);
    index = Seq2Ind(seq_num);
    LockElement(index, kRcvAth);
    if (seq_num <= head_pt) {  // RxWriteTun has already dropped this packet
      printf("Warning ignore already dropped[%u] head_pt[%u]\n", seq_num, head_pt);
      UnLockElement(index);
      return;
    }
    else if (seq_num > head_pt + BUF_SIZE) {
      printf("Wrap around seq_num[%u] head_pt[%u] tail_pt[%u]\n", seq_num, head_pt, tail_pt);
      is_wrap = true;
    }
    Status stat_book = GetElementStatus(index);
    if (stat_book != kOccupiedNew) {/** To prevent out of order reception from the cellular link.*/
      // Push pkt into the queue for rx_write_tun()
      GetPktBufAddr(index, &buf_addr);
      memcpy(buf_addr, pkt, pkt_len);
      UpdateBookKeeping(index, seq_num, kOccupiedNew, pkt_len);
      if ((stat_book == kBlock || stat_book == kDrop) && !is_wrap) {
          SignalElementAvail();
      }
    }
    UnLockElement(index);
  }
  if (is_wrap) {
    set_head_pt(seq_num-1);  
    set_tail_pt(seq_num); 
    for (uint32 i = 0; i < BUF_SIZE; i++) {
      if (i != index) {
        UpdateBookKeeping(i, 0, kEmpty, 0, 0, false);
      }
    }
    SignalElementAvail();
    SignalFill();
    SignalEmpty();
  }
  else if (seq_num > tail_pt) {
    for (uint32 index = tail_pt; index < seq_num; index++) {
      uint32 index_mod = index % BUF_SIZE;
      LockElement(index_mod, kRcvAth);
      if (GetElementStatus(index_mod) == kRead) {  // clear up
        UpdateBookKeeping(index_mod, 0, kEmpty, 0, 0, false);
      }
      UnLockElement(index_mod);
    }
    set_tail_pt(seq_num);  // tail_pt always points to the largest seq num  
    SignalFill();
  }
  UnLockQueue();
}

bool RxDataBuf::DequeuePkt(uint16 *pkt_len, uint8 *pkt) {
  char *buf_addr=NULL;
  uint32 index=0;
  LockQueue();
  while (IsEmpty()) {
    WaitFill();
  }    
  SignalFill();   /** Let RxCreateDataAck to generate ACK.*/
  index = head_pt_mod(); 
  LockElement(index, kWriteTun);
  Status *status = &(GetElementStatus(index));
  uint32 *seq_num = &(GetElementSeqNum(index));
  uint16 *len = &(GetElementLen(index));
  
  bool done = false, is_pkt_available = false;
  *pkt_len = 0;
  while (!done) {
    switch (*status) {
      case kEmpty:
        *status = kBlock;
        GetElementTimeStamp(index).GetCurrTime();
        break;

      case kBlock:
        while (*status == kBlock) {
          UnLockQueue();
          WaitElementAvail(index);
          TIME end;
          end.GetCurrTime();
#ifdef WRT_DEBUG
          printf("Block waiting for seq[%u] duration[%gms]\n", *seq_num, (end - GetElementTimeStamp(index))/1000.);
#endif
          UnLockElement(index);
          LockQueue();
          if (*seq_num != head_pt() + 1) {
            index = head_pt_mod();  
          }
          LockElement(index, kWriteTun);
          status = &(GetElementStatus(index));
          seq_num = &(GetElementSeqNum(index));
          len = &(GetElementLen(index));
        }
        break;

      case kOccupiedNew:
          GetPktBufAddr(index, &buf_addr);
          memcpy(pkt, buf_addr, *len);
          *pkt_len = *len;
          GetElementStatus(index) = kRead;  // Don't change the seq number
          UnLockElement(index);
          IncrementHeadPt();
          SignalEmpty();
          UnLockQueue();
          done = true;
          is_pkt_available = true;
          break;

      case kRead:
      
      case kDrop:
        //printf("<<<Drop pkt[%u]>>>\n", *seq_num);
        UpdateBookKeeping(index, 0, kEmpty, 0, 0, false);
        UnLockElement(index);
        IncrementHeadPt();
        SignalEmpty();
        UnLockQueue();
        done = true;
        break;
    }
  }
  return is_pkt_available;
}

void RxDataBuf::FindNackSeqNum(int block_time, int max_num_nacks, BatchInfo* batch_info, 
        vector<uint32> &nack_seq_arr, uint32 &end_seq) {
  uint32 index=0, head_pt=0, tail_pt=0, end_pt=0, seq_num=0, batch_id; 
  uint32 highest_decoded_seq = 0;  /** Highest decoded seq in the previous batch. */
  uint16 len=0;
  Status stat;
  uint8 num_retrans;
  double interval;
  bool decoding_done = false;
  static uint32 batch_id_prev = 0;

  batch_info->GetBatchID(&batch_id);
  LockQueue();
  //printf("FindNackSeqNum: LockQueue batch_id[%u] batch_id_prev[%u] head_pt[%u] tail_pt[%u]\n", 
  //batch_id, batch_id_prev, RxDataBuf::head_pt(), RxDataBuf::tail_pt());
  if (batch_id <= batch_id_prev) {  /** Wait for new packets available in the buffer. */
    if (IsEmpty()) {
      //printf("FindNackSeqNum: Empty! wait for fill\n");
      WaitFill();
      SignalFill();  /** Make sure DequeuePkt can also proceed.*/
    }
  }  /** else a new batch that we should ack.*/
  head_pt = RxDataBuf::head_pt();
  tail_pt = RxDataBuf::tail_pt();
  UnLockQueue();
  nack_seq_arr.clear();
  batch_info->GetBatchInfo(&highest_decoded_seq, &decoding_done);
  end_pt = tail_pt - 1;
  //printf("FindNackSeqNum: GetBatchInfo head_pt[%u] tail_pt[%u] end_pt[%u] decoding_done[%d] highest_decoded_seq[%u]\n", 
  //head_pt, tail_pt, end_pt, decoding_done, highest_decoded_seq);
  if (!decoding_done) {  
    /** 
     * Only generate ACKs till the highest sequence number of the last batch to 
     * prevent premature retransmission.
     */
    end_seq = highest_decoded_seq;  
    if (end_seq == 0)  /** First batch! Still generate ACK to poke the other side to prevent ACK timeout.*/
      return;
    if (highest_decoded_seq < tail_pt)  /** For the case 1 2 3 4 7 8 with batch size of 5.*/ {
      end_pt = highest_decoded_seq - 1;  /** Generate ACK till the end of the previous batch.*/
      if (head_pt > end_pt) /** All received good. */
        return;
    } 
  }  
  else  /** decoding the current batch is done.*/ {
    end_seq = (highest_decoded_seq > tail_pt) ? highest_decoded_seq : tail_pt;
    batch_info->GetBatchID(&batch_id);
    if (batch_id > batch_id_prev)  /** Start acking the current batch.*/
      batch_id_prev = batch_id;
  }
  //printf("FindNackSeqNum: Update batch id: head_pt[%u] tail_pt[%u] end_pt[%u] batch_id[%u] batch_id_prev[%u] end_seq[%u]\n", 
  //  head_pt, tail_pt, end_pt, batch_id, batch_id_prev, end_seq);
  assert(end_pt < tail_pt);
  TIME start, end;
  end.GetCurrTime();
  for (index = head_pt; index <= end_pt; index++) {
    uint32 index_mod = index % BUF_SIZE;
    LockElement(index_mod, kCreateAck);
    GetBookKeeping(index_mod, &seq_num, &stat, &len, &num_retrans, &start);
    if (stat == kEmpty || stat == kBlock)  /** Detected holes.*/ {
      if (stat == kBlock)
        interval = (end - start)/1000.;
      if (stat == kBlock && interval > block_time) /** Only drop the blocked packet.*/ {
        GetElementStatus(index_mod) = kDrop;
        GetElementSeqNum(index_mod) = index+1;
        SignalElementAvail();
      }
      else /** Insert nacks. */ {
        nack_seq_arr.push_back(index+1);  /** Note can't use seq_num here because this slot could be empty.*/
      }
    }
    UnLockElement(index_mod);
  }
  /** Include the holes at the end of the last batch. */
  for (uint32 i = tail_pt + 1; i <= end_seq; i++) {
    //printf("FindNackSeqNum: extra insert seq[%u]\n", i);
    nack_seq_arr.push_back(i);
  }
  if (nack_seq_arr.size() >= max_num_nacks) {
    //printf("FindNackSeqNum: warning: ack pkt is full cnt[%u]\n", nack_seq_arr.size());
    for (size_t i = 0; i < nack_seq_arr.size(); i++) {
      uint32 drop_ind_mod = (nack_seq_arr[i] - 1) % BUF_SIZE;
      LockElement(drop_ind_mod, kCreateAck);
      GetElementStatus(drop_ind_mod) = kDrop;
      GetElementSeqNum(drop_ind_mod) = nack_seq_arr[i];
      SignalElementAvail();
      UnLockElement(drop_ind_mod);
    }
    nack_seq_arr.clear(); 
  }
}

void RxDataBuf::set_highest_decoded_seq(uint32 seq) {
  assert(seq >= 0);
  LockQueue();
  highest_decoded_seq_ = seq;
  UnLockQueue();
}

void Perror(char *msg, ...) {
  va_list argp;
  va_start(argp, msg);
  vfprintf(stderr, msg, argp);
  va_end(argp);
  exit(-1);
}

// For four headers
void AthHeader::SetRate(uint16 rate) {
  switch(rate) {
     case 10:
      rate_ = ATH5K_RATE_CODE_1M;
      break;
    case 20:
      rate_ = ATH5K_RATE_CODE_2M;
      break;
    case 55:
      rate_ = ATH5K_RATE_CODE_5_5M;
      break;
    case 110:
      rate_ = ATH5K_RATE_CODE_11M;
      break;
    case 60:
      rate_ = ATH5K_RATE_CODE_6M;
      break;
    case 90:
      rate_ = ATH5K_RATE_CODE_9M;
      break;
    case 120:
      rate_ = ATH5K_RATE_CODE_12M;
      break;
    case 180:
      rate_ = ATH5K_RATE_CODE_18M;
      break;
    case 240:
      rate_ = ATH5K_RATE_CODE_24M;
      break;
    case 360:
      rate_ = ATH5K_RATE_CODE_36M;
      break;
    case  480:
      rate_ = ATH5K_RATE_CODE_48M;
      break;
    case 540:
      rate_ = ATH5K_RATE_CODE_54M;
      break;
    default:
      Perror("Error: SetRate() invalid rate[%d]\n", rate);
  }
}

// Return rate * 10
uint16 AthHeader::GetRate() {
  switch(rate_) {
     case ATH5K_RATE_CODE_1M:
      return 10;
    case ATH5K_RATE_CODE_2M:
      return 20;
    case ATH5K_RATE_CODE_5_5M:
      return 55;
    case ATH5K_RATE_CODE_11M:
      return 110;
    case ATH5K_RATE_CODE_6M:
      return 60;
    case  ATH5K_RATE_CODE_9M:
      return 90;
    case ATH5K_RATE_CODE_12M:
      return 120;
    case ATH5K_RATE_CODE_18M:
      return 180;
    case ATH5K_RATE_CODE_24M:
      return 240;
    case ATH5K_RATE_CODE_36M:
      return 360;
    case  ATH5K_RATE_CODE_48M:
      return 480;
    case ATH5K_RATE_CODE_54M:
      return 540;
    default:
      Perror("Error: GetRate() invalid rate[%d]\n", rate_);
  }
}

void AthDataHeader::SetAthHdr(uint32 seq_num, uint16 len, uint16 rate) {
  type_ = ATH_DATA;
  if (len > PKT_SIZE - ATH_DATA_HEADER_SIZE) {    //PKT_SIZE is the MTU of tun0
    Perror("Error: SetAthHdr pkt size: %d ATH_DATA_HEADER_SIZE: %d is too large!\n", 
         len, ATH_DATA_HEADER_SIZE);
  }
  else {
    len_ = len;         
  }
  seq_num_ = seq_num;
  rate_ = rate;
}

void AthDataHeader::ParseAthHdr(uint32 *seq_num, uint16 *len, char *rate) {
  if (type_ != ATH_DATA) {
    Perror("ParseAthHdr error! type_: %d != ATH_DATA\n", type_);
  }
  if (len_ > (PKT_SIZE - ATH_DATA_HEADER_SIZE)) {
    Perror("Error: ParseAthHdr pkt size: %d ATH_DATA_HEADER_SIZE: %d is too large!\n", 
         len_, ATH_DATA_HEADER_SIZE);
  }
  else {
    *len = len_;         
  }
  *seq_num = seq_num_;
  *rate = rate_;
}

/** FEC vdm */
void AthCodeHeader::SetHeader(uint32 batch_id, uint32 start_seq, char type, int ind,
                              int k, int n, const uint16 *len_arr, int bs_id, int client_id) {
  assert(batch_id > 0 && start_seq > 0 && ind >= 0 && ind < n && k >= 0 && n >= 0 && k <= n);
  batch_id_ = batch_id;
  type_ = type;
  start_seq_ = start_seq;
  ind_ = ind;
  k_ = k;
  n_ = n;
  bs_id_ = bs_id;
  client_id_ = client_id;
  memcpy((uint8*)this + ATH_CODE_HEADER_SIZE, len_arr, k_ * sizeof(uint16));
}

void AthCodeHeader::ParseHeader(uint32 *batch_id, uint32 *start_seq, int *ind,
                                int *k, int *n, int *bs_id, int *client_id) const {
  assert(batch_id_ > 0 && start_seq_ > 0 && ind_ < n_ && k_ <= n_ && k_ > 0 && k_ <= MAX_BATCH_SIZE);
  *batch_id = batch_id_;
  *start_seq = start_seq_;
  *ind = ind_;
  *k = k_;
  *n = n_;
  *bs_id = bs_id_;
  *client_id = client_id_;
}

/** GPS Header.*/
void GPSHeader::Init(double time, double latitude, double longitude, double speed, int client_id) {
  assert(speed >= 0);
  seq_++;
  type_ = GPS;
  time_ = time;
  latitude_ = latitude;
  longitude_ = longitude;
  speed_ = speed;
  client_id_ = client_id;
}

/** GPSLogger.*/
GPSLogger::GPSLogger() : fp_(NULL) {}

GPSLogger::~GPSLogger() {
  if (fp_ && fp_ != stdout)
    fclose(fp_);
}

void GPSLogger::ConfigFile(const char* filename) {
  if (filename) {
    filename_ = filename;
    fp_ = fopen(filename_.c_str(), "w");
    assert(fp_);
    fprintf(fp_, "###Seq\tTime\tLatitude\tLongitude\tSpeed\tClientID\n");
    fflush(fp_);
  }
  else {
    fp_ = stdout;
  }
}

void GPSLogger::LogGPSInfo(const GPSHeader &hdr) {
  if (fp_ == stdout)
    fprintf(fp_, "GPS pkt: ");
  fprintf(fp_, "%d\t%.0f\t%.6f\t%.6f\t%.3f\n", hdr.seq_, hdr.time_, hdr.latitude_, hdr.longitude_, hdr.speed_, hdr.client_id_);
  fflush(fp_);
}

/**
 * Parse the nack packet.
 * @param [out] type: either ACK or RAW_ACK.
 * @param [out] ack_seq: Sequence number of this ack packet.
 * @param [out] num_nacks: number of nacks in this ACK packet.
 * @param [out] end_seq: Highest sequence number of the good packets.
 * @param [out] seq_arr: Array of sequence numbers of lost packets.
*/ 
void AckPkt::ParseNack(char *type, uint32 *ack_seq, uint16 *num_nacks, uint32 *end_seq, int* client_id, int* radio_id, uint32 *seq_arr, uint16 *num_pkts) {
  *type = ack_hdr_.type_;
  *ack_seq = ack_hdr_.ack_seq_;
  *num_nacks = ack_hdr_.num_nacks_;
  *end_seq = ack_hdr_.end_seq_;
  *client_id = ack_hdr_.client_id_;
  *radio_id = ack_hdr_.radio_id_;
  if (num_pkts)
    *num_pkts = ack_hdr_.num_pkts_;
  for (int i = 0; i < ack_hdr_.num_nacks_; i++) {
    seq_arr[i] = (uint32)(rel_seq_arr_[i] + ack_hdr_.start_nack_seq_);
  }
}

/**
 * Print the nack packet info.
 */ 
void AckPkt::Print() {
  if (ack_hdr_.type_ == DATA_ACK)
    printf("data_ack:");
  else
    printf("raw_ack:");
  printf("client_id[%d] radio_id[%d] seq[%u] end_seq[%u] num_nacks[%u] num_pkts[%u] {", 
      ack_hdr_.client_id_, ack_hdr_.radio_id_, ack_hdr_.ack_seq_, ack_hdr_.end_seq_, ack_hdr_.num_nacks_, ack_hdr_.num_pkts_);
  for (int i = 0; i < ack_hdr_.num_nacks_; i++) {
    printf("%u ", ack_hdr_.start_nack_seq_ + rel_seq_arr_[i]);
  }
  printf("}\n");
}

bool RxRawBuf::PushPkts(uint32 cur_seq, bool is_cur_good, int bs_id) {
  Lock();
  if (bs_id_ == bs_id) { // When this radio is still served by the same bs
    if (cur_seq <= end_seq_) {  /** Receive out of order packets.*/
      /** This shouldn't happen at raw packets over wireless. */
      UnLock();
      return false;  
    }
    //printf("--Push good seq[%u] end_seq[%u] bs_id[%d]\n", cur_seq, end_seq_, bs_id);
    if (cur_seq - end_seq_ <= kMaxBufSize - pkt_cnt_) {
      for (uint32 i = end_seq_+1; i < cur_seq; i++) {
        nack_deq_.push_back(RawPktRcvStatus(i, 0));
      }
      pkt_cnt_ += (cur_seq - end_seq_);
    } else {
      /*printf("RxRawBuf: Warning: RxRawBuf Full! end_seq[%u] cur_seq[%u] pkt_cnt[%u]\n", 
      end_seq_, cur_seq, pkt_cnt_);*/
      nack_deq_.clear();
      pkt_cnt_ = 1;
    }
    if (!is_cur_good) {
      nack_deq_.push_back(RawPktRcvStatus(cur_seq, 0));
    }
    end_seq_ = cur_seq;
    SignalFill();
  } else { // Initialization or serving bs has changed
    bs_id_ = bs_id;
    nack_deq_.clear();
    pkt_cnt_ = 0;
    end_seq_ = cur_seq;
  }
  UnLock();
  return true;
}

void RxRawBuf::PopPktStatus(vector<uint32> &seq_vec, uint32 *good_seq, uint16 *num_pkts, int *bs_id, uint32 min_pkt_cnt) {
  deque<RawPktRcvStatus>::iterator it;
  seq_vec.clear();
  Lock();
  while (pkt_cnt_ < min_pkt_cnt) {
    WaitFill(); /** Only support one thread enqueuing and one thread dequeuing.*/
  }
  *good_seq = end_seq_;
  for (it = nack_deq_.begin(); it != nack_deq_.end(); it++) {
    seq_vec.push_back(it->seq_);
    it->send_cnt_++;
  }
  /** Remove packet status from the begining till the last one reaching max cnt.*/
  for (it = nack_deq_.begin(); it != nack_deq_.end(); it++) {
    if (it->send_cnt_ != max_send_cnt_)
      break;
  }
  nack_deq_.erase(nack_deq_.begin(), it);
  *num_pkts = pkt_cnt_;
  pkt_cnt_ = 0;
  *bs_id = bs_id_;
  UnLock();
}

void RxRawBuf::Print() {
  deque<RawPktRcvStatus>::const_iterator it;
  Lock();
  printf("---end_seq[%u] max_send_cnt[%u] pkt_cnt[%u] ---\n", end_seq_, max_send_cnt_, pkt_cnt_);
  for (it = nack_deq_.begin(); it < nack_deq_.end(); it++) {
    it -> Print();
    assert(it->send_cnt_ <= max_send_cnt_);
  }
  UnLock();
}

void PrintPkt(char *pkt, uint16 len) {
  for (int i = 0; i < len; i++) {
    printf("%d ", pkt[i]);
  }
  printf("\n");
}

BatchInfo::BatchInfo() : batch_id_(0), highest_decoded_seq_(0), decoding_done_(false) {
  Pthread_mutex_init(&lock_, NULL);
}

BatchInfo::~BatchInfo() {
  Pthread_mutex_destroy(&lock_);
}

void BatchInfo::SetBatchInfo(uint32 batch_id, uint32 seq, bool decoding_done, int ind, int n, uint32 pkt_duration, int bs_id) {
  Lock();
  batch_id_ = batch_id;
  highest_decoded_seq_ = seq; 
  decoding_done_ = decoding_done;
  bs_id_ = bs_id;
  UpdateTimeLeft(ind, n, pkt_duration, false/*don't lock*/);
  UnLock();
}

void BatchInfo::GetBatchInfo(uint32 *seq, bool *is_decoding_done) {
  Lock();
  *seq = highest_decoded_seq_;
  *is_decoding_done = decoding_done(false);
  UnLock();
}

void BatchInfo::GetBSId(int* bs_id) {
  Lock();
  *bs_id = bs_id_;
  UnLock();
}

bool BatchInfo::decoding_done(bool is_lock) {
  bool is_decoding_done = false;
  if (is_lock)
    Lock();
  if (decoding_done_ == false) {
    decoding_done_ = IsTimeOut();  
    /** If timeout happens, decoding this batch is also done.*/
#ifdef WRT_DEBUG
    printf("Check timeout decoding_done: %d\n", decoding_done_);
#endif
  }
  is_decoding_done = decoding_done_;
  if (is_lock)
    UnLock();
  return is_decoding_done;
}

void BatchInfo::set_decoding_done(bool decoding_done) {
  Lock();
  decoding_done_ = decoding_done;
  UnLock();
}

void BatchInfo::GetBatchID(uint32 *batch_id) {
  Lock();
  *batch_id = batch_id_;
  UnLock();
}
  
void BatchInfo::UpdateTimeLeft(int ind, int n, uint32 pkt_duration, bool is_lock) {
  assert(ind >= 0 && ind < n && n > 0);
  if (is_lock)
    Lock();
  time_left_ = pkt_duration * (n - ind - 1);
  //printf("UpdateTimeLeft: ind: %d n: %d pkt_duration: %u time_left: %g\n", ind, n, pkt_duration, time_left_);
  cur_recv_time_.GetCurrTime();
  if (is_lock)
    UnLock();
}

bool BatchInfo::IsTimeOut() {
  const double kExtraWaitTime = 0;  /** Wait for 2 ms to ensure decoding current batch is done. */ 
  TIME end_time;
  end_time.GetCurrTime();
  bool is_timeout = (end_time - cur_recv_time_ > time_left_ + kExtraWaitTime); 
  //printf("end_time - cur_recv_time_[%g] time_left[%g]\n", end_time - cur_recv_time_, time_left_ + kExtraWaitTime);
  return is_timeout;
}

