#include "gps_parser.h"

GPSParser::GPSParser() : 
	time_(-1.0), speed_(-1.0), fp_(NULL), fd_(-1), 
	kStartPhrase("\"class\":\"TPV\""), kGPSCMD("gpspipe -w")
{
	location_.latitude = -1.0;
	location_.longitude = -1.0;
}

GPSParser::~GPSParser()
{
	if (fp_) pclose(fp_);
}

void GPSParser::ConfigSpeed(bool is_fixed_speed, double speed)
{
	is_fixed_speed_ = is_fixed_speed;
	if (is_fixed_speed_)
		speed_ = speed;
	else
	{
		fp_ = popen(kGPSCMD.c_str(), "r");
		assert(fp_);
		fd_ = fileno(fp_);
		assert(fd_ > 0);
		fcntl(fd_, F_SETFL, O_NONBLOCK);
	} 
}

bool GPSParser::GetGPSReadings()
{
	char buf[MAX_READ];
	if (is_fixed_speed_)
	{
		assert(speed() >= 0);  /** Ensure we have populated the speed reading.*/
		sleep(1);   /** Report it every sec to emulate the interval of getting GPS.*/
		return true;
	}
	else  /** We have to get the readings from the GPS device.*/
	{
		WaitReadingsAvailable();
		ssize_t nread = read(fd_, buf, MAX_READ-1);
		assert(nread > 0);
		buf[nread] = '\0';
		bool ret = ParseLine(string(buf));
		return ret;
	}
}

void GPSParser::WaitReadingsAvailable()
{
	int ret;
	fd_set rd_set;

	FD_ZERO(&rd_set);
	FD_SET(fd_, &rd_set); 

	ret = select(fd_+1, &rd_set, NULL, NULL, NULL);
	assert(ret > 0);  /** No timeout. It should be available after it returns.*/
}

void GPSParser::SplitLine(const string &line, const string &separator)
{
	size_t start_pos=0, end_pos=0;
	phrases_.clear();
	while ((end_pos = line.find_first_of(separator, start_pos)))
	{
		if (end_pos == string::npos)
		{
			phrases_.push_back(line.substr(start_pos));
			break;
		} 
		phrases_.push_back(line.substr(start_pos, end_pos-start_pos));
		start_pos = end_pos+1;
	}
}

bool GPSParser::GetValue(const string &phrase, const string &key, double *val)
{
	bool found = false;
	string search_key = key + "\":";
	size_t pos = phrase.find(search_key);
	if (pos == string::npos)
		return false;
	string val_str = phrase.substr(pos + search_key.size()); 
	*val = atof(val_str.c_str());
	return true;
}

bool GPSParser::IsValidLine()
{
	if (phrases_.empty())
		return false;
	size_t pos = phrases_[0].find(kStartPhrase);
	return !(pos == string::npos);
}

bool GPSParser::ParseLine(const string &line)
{
	double time = -1.0, lat = -1.0, lon = -1.0, speed = -1.0;
	bool found_time = false, found_lat = false, found_lon = false, found_speed = false;
	SplitLine(line, ",");
	if (!IsValidLine())
		return false;
	size_t sz = phrases_.size();
	for (size_t i = 0; i < sz; i++)
	{
		if (!found_time)
			found_time = GetValue(phrases_[i], "time", &time);
		if (!found_lat)
			found_lat = GetValue(phrases_[i], "lat", &lat);
		if (!found_lon)
			found_lon = GetValue(phrases_[i], "lon", &lon);
		if (!found_speed)
			found_speed = GetValue(phrases_[i], "speed", &speed);
	}
	if (!found_time || !found_lat || !found_lon || !found_speed)
		return false;
	time_ = time;
	location_.latitude = lat;
	location_.longitude = lon;
	speed_ = speed;
	return true;
}

void GPSParser::Print()
{
	printf("===GPS Parser===\n");
	printf("time[%g], lat[%.6f] lon[%.6f] speed[%.3f]\n", 
		time_, location_.latitude, location_.longitude, speed_);
	for (vector<string>::const_iterator it = phrases_.begin(); it < phrases_.end(); it++)
		cout << *it << endl;
}
