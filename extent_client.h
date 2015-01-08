// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <pthread.h>
#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
	public:
		struct extent_cache_t{
			extent_protocol::attr attr;
			std::string content;
			bool isDirty;
			bool isRemoved;
			bool isCached;	
			bool isRemote;	
			extent_cache_t(){
				isDirty = false;
				isRemoved = false;
				isCached = false;
				isRemote = false;
			}
		};
 	private:
  		rpcc *cl;
		pthread_mutex_t extents_cache_m;
		std::map<extent_protocol::extentid_t, struct extent_cache_t> extents_cache;
 	public:
  	
  		extent_client(std::string dst);

  		extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf);
  		extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
  		extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  		extent_protocol::status remove(extent_protocol::extentid_t eid);
 		extent_protocol::status setattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr a);
		extent_protocol::status fetch(extent_protocol::extentid_t eid);
		extent_protocol::status flush(extent_protocol::extentid_t eid);
};

#endif 

