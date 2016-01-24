#include "wspace_client_scout.h"
#include <algorithm>

WspaceClient *wspace_client;

using namespace std;

int main(int argc, char **argv) {
  printf("PKT_SIZE: %d\n", PKT_SIZE);
  const char* args = "T:B:i:I:S:s:C:A:p:n:hd:r:";
  wspace_client = new WspaceClient(argc, argv, args);
  wspace_client->tun_.Init();

  Pthread_create(&wspace_client->p_rx_rcv_ath_, NULL, LaunchRxRcvAth, NULL);
  Pthread_create(&wspace_client->p_rx_write_tun_, NULL, LaunchRxWriteTun, NULL);
  for (map<int, pthread_t>::iterator it = wspace_client->p_rx_create_raw_ack_tbl_.begin(); it != wspace_client->p_rx_create_raw_ack_tbl_.end(); ++it) {
    Pthread_create(&it->second, NULL, LaunchRxCreateRawAck, &it->first);
  }
  Pthread_create(&wspace_client->p_rx_create_data_ack_, NULL, LaunchRxCreateDataAck, NULL);
  Pthread_create(&wspace_client->p_rx_send_cell_, NULL, LaunchRxSendCell, NULL);
  //Pthread_create(&wspace_client->p_rx_parse_gps_, NULL, LaunchRxParseGPS, NULL);

  Pthread_join(wspace_client->p_rx_rcv_ath_, NULL);
  Pthread_join(wspace_client->p_rx_write_tun_, NULL);
  Pthread_join(wspace_client->p_rx_create_front_raw_ack_, NULL);
  Pthread_join(wspace_client->p_rx_create_back_raw_ack_, NULL);
  Pthread_join(wspace_client->p_rx_create_data_ack_, NULL);
  Pthread_join(wspace_client->p_rx_send_cell_, NULL);
  //Pthread_join(wspace_client->p_rx_parse_gps_, NULL);

  delete wspace_client;
  return 0;
}

WspaceClient::WspaceClient(int argc, char *argv[], const char *optstring) 
    : decoder_(CodeInfo::kDecoder, MAX_BATCH_SIZE, PKT_SIZE), min_pkt_cnt_(30) {
  int option;
  double speed = -1.0;
  while ((option = getopt(argc, argv, optstring)) > 0) {
    switch(option) {
      case 'T':
        ack_time_out_ = atoi(optarg);
        printf("ACK_TIME_OUT: %dms\n", ack_time_out_);
        break;
      case 'B':
        block_time_ = atoi(optarg);
        printf("block time: %dms\n", block_time_);
        break;
      case 'i':
        strncpy(tun_.if_name_, optarg, IFNAMSIZ-1);
        tun_.tun_type_ = IFF_TUN;
        break;
      case 'I':
        tun_.client_id_ = atoi(optarg);
        printf("client_id: %d\n", tun_.client_id_);
        break;
      case 'C':
        strncpy(tun_.controller_ip_eth_,optarg,16);
        printf("controller_ip_eth: %s\n", tun_.controller_ip_eth_);
        break;
      case 'A':
        if ( tun_.radio_ids_.size() == 0 )
          Perror("Need to set number of radios before setting raw_pkt_buf_tbl_");
        max_ack_cnt_ = uint8(atoi(optarg));
        for (map<int, RxRawBuf>::iterator it = raw_pkt_buf_tbl_.begin(); it != raw_pkt_buf_tbl_.end(); ++it) {
          it->second.set_max_send_cnt(max_ack_cnt_);
        }
        printf("Max ACK CNT: %u\n", max_ack_cnt_);
        break;
      case 'p':
        speed = atof(optarg);
        if (speed >= 0) gps_parser_.ConfigSpeed(true, speed);  /** Fixed speed for cart experiment. */
        else gps_parser_.ConfigSpeed(false);  /** Get readings from the gps device.*/
        printf("Speed is_fixed[%d] speed[%.3f]\n", (int)gps_parser_.is_fixed_speed(), gps_parser_.speed());
        break; 
      case 'n':
        min_pkt_cnt_ = atoi(optarg);
        printf("min_pkt_cnt: %d\n", min_pkt_cnt_);
        assert(min_pkt_cnt_ > 0);
        break;
      case 's': {
        string addr;
        stringstream ss(optarg);
        while(getline(ss, addr, ',')) {
          if(atoi(addr.c_str()) == 1)
              Perror("id 1 is reserved by controller\n");
          bs_ids_.push_back(atoi(addr.c_str()));
        }
        break;
      }
      case 'S': {
        ParseIP(bs_ids_, tun_.bs_ip_tbl_, tun_.bs_addr_tbl_);
        break;
      }
      case 'r': {
        string radio;
        stringstream ss(optarg);
        while(getline(ss, radio, ',')) {
          tun_.radio_ids_.push_back(atoi(radio.c_str()));
        }
        break;
      }
      default:
        Perror("Usage: %s -i tun0/tap0 -S server_eth_ip -s server_ath_ip -C client_eth_ip\n",argv[0]);
    }
  }
  assert(tun_.if_name_[0] && tun_.controller_ip_eth_[0] && tun_.client_id_ && tun_.bs_ip_tbl_.size() && tun_.radio_ids_.size());
  for (map<int, string>::iterator it = tun_.bs_ip_tbl_.begin(); it != tun_.bs_ip_tbl_.end(); ++it) {
    assert(strlen(it->second.c_str()));
  }
  if(tun_.radio_ids_.size() > MAX_RADIO) {
    Perror("Need to revise 901-base.patch to enable more radios!\n");
  }
}

void WspaceClient::ParseIP(const vector<int> &ids, map<int, string> &ip_table, map<int, struct sockaddr_in> &addr_table) {
  if (ids.empty()) {
    Perror("WspaceClient::ParseIP: Need to indicate ids first!\n");
  }
  vector<int>::const_iterator it = ids.begin();
  string addr;
  stringstream ss(optarg);
  while(getline(ss, addr, ',')) {
    if (it == ids.end())
      Perror("WspaceClient::ParseIP: Too many input addresses\n");
    int id = *it;
    ip_table[id] = addr;
    tun_.CreateAddr(ip_table[id].c_str(), tun_.port_eth_, &addr_table[id]);
    ++it;
  }
}

template<class T>
inline bool IsIndExist(T *arr, int len, T val) {
  T *p = find(arr, arr+len, val);
  return (p < arr+len);
}

void* WspaceClient::RxRcvAth(void* arg) {
  uint32 seq_num=0;
  char *buf_addr=NULL;
  AthCodeHeader *hdr;
  uint32 batch_id=0, batch_id_parse=0, start_seq=0, start_seq_parse=0;
  int coding_index_parse=-1;
  int early_decoding_cnt=0;
  bool decoding_done=false;
  int *early_decoding_inds = new int[MAX_BATCH_SIZE];
  uint16 nread=0;
  TIME start, end;
  char *pkt = new char[PKT_SIZE];
  Tun::IOType type = 0;
  while (1) {
    int k = -1, n = -1;
    int radio_id = 0;
    RcvDownlinkPkt(pkt, &nread, &type, &radio_id);

    // Get the header info
    hdr = (AthCodeHeader*)pkt;

#ifdef RAND_DROP
    if (!hdr->is_good()) {
      continue;
    }
#endif

    if (type == kWspace && radio_ids_.count(radio_id))
      raw_pkt_buf_tbl_[radio_id].PushPkts(hdr->raw_seq(), true/**is good*/);  
    /** else, do nothing for the cellular case.*/

    hdr->ParseHeader(&batch_id_parse, &start_seq_parse, &coding_index_parse, &k, &n);
#ifdef WRT_DEBUG
    printf("Receive: radio_id: %d raw_seq: %u batch_id: %u seq_num: %u start_seq: %u coding_index: %d k: %d n: %d\n", 
    radio_id, hdr->raw_seq(), batch_id_parse, start_seq_parse + coding_index_parse, start_seq_parse, 
    coding_index_parse, k, n);
#endif
    uint32 per_pkt_duration = (nread * 8.0) / (hdr->GetRate() / 10.0) + DIFS_80211ag + SLOT_TIME * 5;  /** in us.*/

    if (batch_id > batch_id_parse) {  /** Out of batch order.*/
      if (type == kCellular) {
        assert(coding_index_parse < k);
        seq_num = start_seq_parse + coding_index_parse;
        uint16 len = hdr->lens()[coding_index_parse];
        data_pkt_buf_.EnqueuePkt(seq_num, len, (char*)hdr->GetPayloadStart());
#ifdef WRT_DEBUG
        printf("Cellular out of order enqueue: batch_id_parse[%u] batch_id[%u] seq_num[%u] len[%u]\n", 
        batch_id_parse, batch_id, seq_num, len);
#endif
      } 
      continue; /** Stale batch, move on. */
    }
    else if (batch_id < batch_id_parse) {  
      /** This is a new batch. */
      batch_id = batch_id_parse;
      decoding_done = false;
      early_decoding_cnt = 0;
      decoder_.ClearInfo();
      decoder_.SetCodeInfo(k, n, start_seq_parse);
      bzero(early_decoding_inds, MAX_BATCH_SIZE*sizeof(int));
      decoder_.CopyLens(hdr->lens());
      /** Prevent the start seq to be set to a smaller value due ot retransission.*/
      start_seq = (start_seq_parse > start_seq) ? start_seq_parse : start_seq;  
      batch_info_.SetBatchInfo(batch_id_parse, start_seq-1, decoding_done, coding_index_parse, n, per_pkt_duration);
      start.GetCurrTime();
    }
    else {  /** Handling the current batch. */
      /** Wait for the front antenna's transmisison to be done.*/
      if (!decoding_done)
        batch_info_.UpdateTimeLeft(coding_index_parse, n, per_pkt_duration);  
      if (decoding_done || IsIndExist(decoder_.inds(), decoder_.coding_pkt_cnt(), coding_index_parse)) {
        continue;  /** Not a useful packet. Could be due to two antenna combining*/
      }
    }

    assert(decoder_.PushPkt(nread - hdr->GetFullHdrLen(), hdr->GetPayloadStart(), coding_index_parse));
//    printf("Push encoding pkt cnt[%d] start_seq[%u] coding_index[%d] raw_seq[%u]\n", 
//      decoder_.coding_pkt_cnt(), start_seq_parse, coding_index_parse, hdr->raw_seq());
    /**
     * Enqueue the native packets before waiting for the full batch to decode. 
     * It benefits most under partial decoding failure.
     */
    if (coding_index_parse < k) {  
      seq_num = start_seq_parse + coding_index_parse;
      data_pkt_buf_.EnqueuePkt(seq_num, decoder_.GetLen(coding_index_parse), (char*)hdr->GetPayloadStart());
      early_decoding_inds[early_decoding_cnt] = coding_index_parse;
#ifdef WRT_DEBUG
      printf("Early enqueue pkt batch_id: %u seq_num: %u start_seq: %u index: %d len: %d\n", 
        batch_id, seq_num, start_seq_parse, coding_index_parse, decoder_.GetLen(coding_index_parse));
#endif
      early_decoding_cnt++;
    } 

    if (decoder_.coding_pkt_cnt() >= k) {  /** Have k unique coding packets for decoding. Shouldn't be greater though. */
      uint32 decoding_duration;
      if (early_decoding_cnt != k) {  /** We need to recover some packets.*/
        decoder_.DecodeBatch();
        for (int i = 0; i < k; i++) {
          if (!IsIndExist(early_decoding_inds, early_decoding_cnt, i)) {
            uint16 native_pkt_len=-1;
            seq_num = start_seq_parse + i;
            assert(decoder_.PopPkt((uint8**)&buf_addr, &native_pkt_len));
            //printf("decode pkt batch_id: %u seq_num: %u len: %d\n", batch_id, seq_num, native_pkt_len);
            data_pkt_buf_.EnqueuePkt(seq_num, native_pkt_len, buf_addr);
          }
          else {
            decoder_.MoveToNextPkt();
          }
        }
        end.GetCurrTime();
        printf("Decoding duration: %gms\n", (end - start)/1000.);
      }
      decoding_done = true;
      batch_info_.set_decoding_done(decoding_done);
    }
  }

  delete[] early_decoding_inds;
  delete[] pkt;
  return (void*)NULL;
}

void* WspaceClient::RxWriteTun(void* arg) {
  char *pkt = new char[PKT_SIZE];
  bool is_pkt_available = false;
  uint16 len = 0;
  while (1) {
    bzero(pkt, PKT_SIZE);
    is_pkt_available = data_pkt_buf_.DequeuePkt(&len, (uint8*)pkt);
    if (is_pkt_available) {
      ControllerToClientHeader* hdr = (ControllerToClientHeader*)pkt;
      if (hdr->type() == CONTROLLER_TO_CLIENT && hdr->client_id() == tun_.client_id_) {
        tun_.Write(Tun::kTun, pkt + sizeof(ControllerToClientHeader), len - sizeof(ControllerToClientHeader), 0);
      }
    }
  }
  delete[] pkt;
  return (void*)NULL;
}

void* WspaceClient::RxCreateDataAck(void* arg) {
  AckPkt *ack_pkt = new AckPkt;
  vector<uint32> nack_seq_arr;
  uint32 end_seq=0;
  int bs_id = bs_ids_.front(); // TODO: Enable dynamically assignment of bs_id.
  while (1) {
    usleep(ack_time_out_*1000);
    data_pkt_buf_.FindNackSeqNum(block_time_, ACK_WINDOW, batch_info_, nack_seq_arr, end_seq);
    ack_pkt->Init(DATA_ACK);  /** Data ack for retransmission. */
    ack_pkt->set_ids(tun_.client_id_, 0);
    for (vector<uint32>::iterator it = nack_seq_arr.begin(); it != nack_seq_arr.end(); it++)
      ack_pkt->PushNack(*it);
    ack_pkt->set_end_seq(end_seq);
    tun_.Write(Tun::kCellular, (char*)ack_pkt, ack_pkt->GetLen(), bs_id);
#ifdef WRT_DEBUG
    ack_pkt->Print();
#endif
  }
  delete ack_pkt;
}

void* WspaceClient::RxCreateRawAck(void* arg) {
  char ack_type = 0;
  vector<uint32> nack_vec;
  vector<uint32>::const_iterator it;
  uint32 end_seq = 0;
  uint16 num_pkts = 0;
  RxRawBuf *raw_buf;
  AckPkt *ack_pkt = new AckPkt;
  int *radio_id = (int*)arg;
  int bs_id = bs_ids_.front(); // TODO: Enable dynamically assignment of bs_id.

  raw_buf = &raw_pkt_buf_tbl_[*radio_id];
  ack_type = RAW_ACK;

  while (1) {
    ack_pkt->Init(ack_type);
    ack_pkt->set_ids(tun_.client_id_, *radio_id);
    raw_buf->PopPktStatus(nack_vec, &end_seq, &num_pkts, (uint32)min_pkt_cnt_);
    for (it = nack_vec.begin(); it != nack_vec.end(); it++) {
      ack_pkt->PushNack(*it);
      if (ack_pkt->IsFull()) {
        Perror("Error: Raw nack full!\n");
      }
    }
    ack_pkt->set_end_seq(end_seq);
    ack_pkt->set_num_pkts(num_pkts);
    tun_.Write(Tun::kCellular, (char*)ack_pkt, ack_pkt->GetLen(), bs_id);
    //ack_pkt->Print();
  }
  delete ack_pkt;
};

void* WspaceClient::RxSendCell(void* arg) {
  uint16 nread=0;
  char *pkt = new char[BUF_SIZE];
  CellDataHeader cell_hdr;
  while (1) {
    nread = tun_.Read(Tun::kTun, &(pkt[CELL_DATA_HEADER_SIZE]), PKT_SIZE-CELL_DATA_HEADER_SIZE);
    memcpy(pkt, (char*)&cell_hdr, CELL_DATA_HEADER_SIZE);
    tun_.Write(Tun::kController, pkt, nread+CELL_DATA_HEADER_SIZE, 0);
  }
  delete[] pkt;
}

void* WspaceClient::RxParseGPS(void* arg) {
  GPSHeader gps_hdr;
  GPSLogger gps_logger;
  gps_logger.ConfigFile();
  int bs_id = bs_ids_.front(); // TODO: Enable dynamically assignment of bs_id.
  while (true) {
    bool is_available = gps_parser_.GetGPSReadings();
    if (is_available) {
      gps_hdr.Init(gps_parser_.time(), gps_parser_.location().latitude, 
          gps_parser_.location().longitude, gps_parser_.speed());
      tun_.Write(Tun::kCellular, (char*)&gps_hdr, GPS_HEADER_SIZE, bs_id);
      //gps_logger.LogGPSInfo(gps_hdr);
    }
  }
}

void WspaceClient::RcvDownlinkPkt(char *pkt, uint16 *len, Tun::IOType *type_out, int *radio_id) {
  vector<Tun::IOType> type_arr;
  Tun::IOType type_out;

  type_arr.push_back(Tun::kWspace);
  type_arr.push_back(Tun::kCellular);

  *len = tun_.Read(type_arr, pkt, PKT_SIZE, &type_out, radio_id);
  assert(type_out == Tun::kWspace || type_out == Tun::kCellular);
}

void* LaunchRxRcvAth(void* arg) {
  wspace_client->RxRcvAth(arg);
}

void* LaunchRxWriteTun(void* arg) {
  wspace_client->RxWriteTun(arg);
}

void* LaunchRxCreateDataAck(void* arg) {
  wspace_client->RxCreateDataAck(arg);
}

void* LaunchRxCreateRawAck(void* arg) {
  wspace_client->RxCreateRawAck(arg);
}

void* LaunchRxSendCell(void* arg) {
  wspace_client->RxSendCell(arg);
}

void* LaunchRxParseGPS(void* arg) {
  wspace_client->RxParseGPS(arg);
}
