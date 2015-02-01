// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent
//constructor
extent_client::extent_client(std::string dst)
{
  sockaddr_in dstsock;
	make_sockaddr(dst.c_str(), &dstsock);
  cl = new rpcc(dstsock);
  if (cl->bind() != 0) {
    printf("extent_client: bind failed\n");
  }
  pthread_mutex_init(&extents_cache_m,NULL);
}
//fetch extent content and attributes together from server
extent_protocol::status 
extent_client::fetch(extent_protocol::extentid_t eid)
{
	extent_protocol::status ret = extent_protocol::OK;
	
	std::string buf;
	//get() RPC call to the server	
  	ret = cl->call(extent_protocol::get, eid, buf);
	if(ret != extent_protocol::OK)
		return ret;
	extent_protocol::attr attr;
	//getattr() RPC call to the server
	ret = cl->call(extent_protocol::getattr, eid, attr);
	if(ret != extent_protocol::OK)
		return ret;
	
	std::cout << "fectch " << "content=" << buf << std::endl; 
	extents_cache[eid].content = buf;
	extents_cache[eid].attr = attr;
	extents_cache[eid].isCached = true;		//this extent is cached now
	extents_cache[eid].isRemote = true;		//this extent also exists at the server

	return ret;
}
//get the content of an extent from local cache or server
extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	//if not cached, fetch it from server
	if(!extents_cache[eid].isCached){
		ret = fetch(eid);
		if(ret != extent_protocol::OK){
			pthread_mutex_unlock(&extents_cache_m);
			return ret;
		}
	}
	//return error if this extent has been removed 
	if(extents_cache[eid].isRemoved){
		pthread_mutex_unlock(&extents_cache_m);
		return extent_protocol::NOENT;
	}
	//modify the access time of this extent
	extents_cache[eid].attr.atime = time(NULL);
	buf = extents_cache[eid].content;

	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}
//get the attribute of an extent from local cache or server
extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	//if nor cached, fetch it from server
	if(!extents_cache[eid].isCached){
		ret = fetch(eid);
		if(ret != extent_protocol::OK){
			pthread_mutex_unlock(&extents_cache_m);
			return ret;
		}
	}
	//return error if it has been removed
	if(extents_cache[eid].isRemoved){
		pthread_mutex_unlock(&extents_cache_m);
		return extent_protocol::NOENT;
	}
	attr = extents_cache[eid].attr;

	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}
//replace the content of an extent
extent_protocol::status
extent_client::put(extent_protocol::extentid_t eid, std::string buf)
{
  	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
  	
	extents_cache[eid].content = buf;
	extents_cache[eid].attr.size = buf.size();
	time_t curTime = time(NULL);
	extents_cache[eid].attr.mtime = curTime;
	extents_cache[eid].attr.ctime = curTime;
	
	extents_cache[eid].isCached = true;	//if this extent is newly created, we need to modify the state to be cached
	extents_cache[eid].isDirty = true;	//this extent has been modified

	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}
//remove an extent in local cache
extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  	extent_protocol::status ret = extent_protocol::OK;
  	pthread_mutex_lock(&extents_cache_m);
  	
	extents_cache[eid].isRemoved = true;   //this extent has been removed
	extents_cache[eid].isCached = true;	   //note this extent should be still cached,if not, will fetch it from server, which is wrong
	extents_cache[eid].isDirty = true;	   
	
	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}
//set the attribute of an extent in local cache
extent_protocol::status 
extent_client::setattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr a)
{
  	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	//if not cached, fetch it from server
	if(!extents_cache[eid].isCached){
		ret = fetch(eid);
		if(ret != extent_protocol::OK){
			pthread_mutex_unlock(&extents_cache_m);
			return ret;
		}
	}
	//we only consider the change of size attribute, truncate the string if size becomes smaller or add characters to the end if size becomes larger
	if(extents_cache[eid].attr.size != a.size){
		extents_cache[eid].content.resize(a.size);
		extents_cache[eid].attr.mtime = time(NULL);
		extents_cache[eid].attr.ctime = time(NULL);
		extents_cache[eid].isDirty = true;
	}

		
	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}
//flush the extents to the server, note that this only happen just before an extent try to release its lock to lock server, to achieve that every get() reads the latest content
extent_protocol::status 
extent_client::flush(extent_protocol::extentid_t eid)
{
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	int r;
	//only flush dirty caches
	if(extents_cache[eid].isDirty){
		printf("extent_client: Flush %d", eid);
		//if this extent has been removed and exists at the server, apply remove RPC call
		if(extents_cache[eid].isRemoved && extents_cache[eid].isRemote){
			ret = cl->call(extent_protocol::remove, eid, r);
		}
		//if not removed
		else{
			//put the content and attribute together by applying put RPC call to the server
			ret = cl->call(extent_protocol::put, eid, extents_cache[eid].content, 
							extents_cache[eid].attr, r);
			std::cout << extents_cache[eid].content << std::endl;
		}

	}
	//clear this extent after flush it to server
	extent_protocol::attr attr;
	extents_cache[eid].content = "";
	extents_cache[eid].attr = attr;
	extents_cache[eid].isDirty = false;
	extents_cache[eid].isCached = false;
	extents_cache[eid].isRemote = false;
	extents_cache[eid].isRemoved = false;
	pthread_mutex_unlock(&extents_cache_m);
	return ret;
}





