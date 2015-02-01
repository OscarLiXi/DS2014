// this is the extent server

#ifndef extent_server_h
#define extent_server_h

#include <string>
#include <map>
#include "extent_protocol.h"

#define IS_FILE 0x80000000
#define DEBUG 1

class extent_server {
 public:
/*	struct fileInfo{
		extent_protocol::attr fAttr;
		std::string fContent;
	};
	
	struct dirInfo{
		extent_protocol::attr dAttr;
		std::string dContent; //Including extent ids inside this dir
	};
*/
	struct extentInfo{
		extent_protocol::attr eAttr;
		std::string content; //Including extent ids inside this dir
	};
 private:
	//std::map<extent_protocol::extentid_t, struct fileInfo> files;
	//std::map<extent_protocol::extentid_t, struct dirInfo> dirs;
	std::map<extent_protocol::extentid_t, struct extentInfo> extents;

 public:
  extent_server();

  //int put(extent_protocol::extentid_t id, std::string, int &);
  int put(extent_protocol::extentid_t id, std::string, extent_protocol::attr, int &);
  int get(extent_protocol::extentid_t id, std::string &);
  int getattr(extent_protocol::extentid_t id, extent_protocol::attr &);
  int remove(extent_protocol::extentid_t id, int &);

  int setattr(extent_protocol::extentid_t id, extent_protocol::attr, int &);
};

#endif 







