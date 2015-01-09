// extent client interface.

#ifndef extent_client_h
#define extent_client_h

#include <pthread.h>
#include <string>
#include "extent_protocol.h"
#include "rpc.h"

class extent_client {
	public:
		//the structure for local extent cache
		struct extent_cache_t{
			extent_protocol::attr attr;  //extent attribute
			std::string content;		//extent content
			bool isDirty;				//track if extent cache has been modified
			bool isRemoved;				//track if extent cache has been modified
			bool isCached;				//track if extent cache is cached or not
			bool isRemote;				//track if extent cache is at server or not
			extent_cache_t(){
				isDirty = false;
				isRemoved = false;
				isCached = false;
				isRemote = false;
			}
		};
 	private:
  		rpcc *cl;
		//to lock the extents_cache since multiple thread might access it
		pthread_mutex_t extents_cache_m;
		//maintain the extents cache in a map with extent id as the key 
		std::map<extent_protocol::extentid_t, struct extent_cache_t> extents_cache;
 	public:
  	
  		extent_client(std::string dst);
		//get the content of an extent from local cache or server
  		extent_protocol::status get(extent_protocol::extentid_t eid, 
			      std::string &buf); 
		//get the attribute of an extent from local cache or server
  		extent_protocol::status getattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr &a);
		//put(replace) the content of an extent in local cache
  		extent_protocol::status put(extent_protocol::extentid_t eid, std::string buf);
  		//remove an extent in local cache
		extent_protocol::status remove(extent_protocol::extentid_t eid);
		//set the attribute of an extent in local cache
 		extent_protocol::status setattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr a);
		//fetch the extent from server
		extent_protocol::status fetch(extent_protocol::extentid_t eid);
		//flush extent to server
		extent_protocol::status flush(extent_protocol::extentid_t eid);
};

#endif 

