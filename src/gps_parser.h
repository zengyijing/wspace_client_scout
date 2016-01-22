#ifndef GPS_PARSER_H_
#define GPS_PARSER_H_

#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <sys/select.h>
#include "pthread_wrapper.h"

#define MAX_READ 2000

struct Location {
  double latitude;
  double longitude;
};

using namespace std;

class GPSParser {
 public:
  GPSParser();
  ~GPSParser();
  
  /** 
   * @return true - if speed reading is available.
   */
  bool GetGPSReadings();

  double time() const { return time_; }

  double speed() const { return speed_; }

  bool is_fixed_speed() const { return is_fixed_speed_; }

  void ConfigSpeed(bool is_fixed_speed, double speed = -1.0);

  const Location& location() const { return location_; }

  void Print();

 private:
  void WaitReadingsAvailable();

  bool ParseLine(const string &line);

  void SplitLine(const string &line, const string &separator=" ,\t");

  bool IsValidLine();

  bool GetValue(const string &phrase, const string &key, double *val);

  void ConfigGPSPipe();

  const string kStartPhrase;
  const string kGPSCMD;
  double time_;
  Location location_;
  double speed_;
  bool is_fixed_speed_;
  FILE *fp_;
  int fd_;  /** Need for the non-blocking socket. */
  vector<string> phrases_;
};

#endif
