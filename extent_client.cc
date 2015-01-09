// RPC stubs for clients to talk to extent_server

#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

// The calls assume that the caller holds a lock on the extent

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

extent_protocol::status 
extent_client::fetch(extent_protocol::extentid_t eid)
{
	extent_protocol::status ret = extent_protocol::OK;
	
	std::string buf;	
  	ret = cl->call(extent_protocol::get, eid, buf);
	if(ret != extent_protocol::OK)
		return ret;
	extent_protocol::attr attr;
	ret = cl->call(extent_protocol::getattr, eid, attr);
	if(ret != extent_protocol::OK)
		return ret;
	
	std::cout << "fectch " << "content=" << buf << std::endl; 
	extents_cache[eid].content = buf;
	extents_cache[eid].attr = attr;
	extents_cache[eid].isCached = true;
	extents_cache[eid].isRemote = true;

	return ret;
}

extent_protocol::status
extent_client::get(extent_protocol::extentid_t eid, std::string &buf)
{
  	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	
	if(!extents_cache[eid].isCached){
		ret = fetch(eid);
		if(ret != extent_protocol::OK){
			pthread_mutex_unlock(&extents_cache_m);
			return ret;
		}
	}
	extents_cache[eid].attr.atime = time(NULL);
	buf = extents_cache[eid].content;

	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}

extent_protocol::status
extent_client::getattr(extent_protocol::extentid_t eid, 
		       extent_protocol::attr &attr)
{
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	
	if(!extents_cache[eid].isCached){
		ret = fetch(eid);
		if(ret != extent_protocol::OK){
			pthread_mutex_unlock(&extents_cache_m);
			return ret;
		}
	}
	if(extents_cache[eid].isRemoved){
		pthread_mutex_unlock(&extents_cache_m);
		return extent_protocol::NOENT;
	}
	attr = extents_cache[eid].attr;

	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}

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
	
	extents_cache[eid].isCached = true;
	extents_cache[eid].isDirty = true;	

	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}

extent_protocol::status
extent_client::remove(extent_protocol::extentid_t eid)
{
  	extent_protocol::status ret = extent_protocol::OK;
  	pthread_mutex_lock(&extents_cache_m);
  	
	extents_cache[eid].isRemoved = true;
	extents_cache[eid].isCached = true;
	extents_cache[eid].isDirty = true;
	
	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}

extent_protocol::status 
extent_client::setattr(extent_protocol::extentid_t eid, 
				  extent_protocol::attr a)
{
  	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);

	if(!extents_cache[eid].isCached){
		ret = fetch(eid);
		if(ret != extent_protocol::OK){
			pthread_mutex_unlock(&extents_cache_m);
			return ret;
		}
	}

	if(extents_cache[eid].attr.size != a.size){
		extents_cache[eid].content.resize(a.size);
		extents_cache[eid].attr.mtime = time(NULL);
		extents_cache[eid].attr.ctime = time(NULL);
		extents_cache[eid].isDirty = true;
	}

		
	pthread_mutex_unlock(&extents_cache_m);
  	return ret;
}

extent_protocol::status 
extent_client::flush(extent_protocol::extentid_t eid)
{
	extent_protocol::status ret = extent_protocol::OK;
	pthread_mutex_lock(&extents_cache_m);
	int r;
	if(extents_cache[eid].isDirty){
		printf("extent_client: Flush %d", eid);
		if(extents_cache[eid].isRemoved && extents_cache[eid].isRemote){
			ret = cl->call(extent_protocol::remove, eid, r);
		}
		else{
			ret = cl->call(extent_protocol::put, eid, extents_cache[eid].content, 
							extents_cache[eid].attr, r);
			std::cout << extents_cache[eid].content << std::endl;
		}

	}
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





