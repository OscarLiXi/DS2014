// the extent server implementation

#include "extent_server.h"
#include <sstream>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extent_server::extent_server() {
	//Initilize a new dir with extentid = 1
	extentInfo rootDir;
	time_t curTime = time(NULL);
	rootDir.eAttr.atime = curTime;
	rootDir.eAttr.mtime = curTime;
	rootDir.eAttr.ctime = curTime;
	rootDir.eAttr.size = 0; //We simply ignore the size of a dir
	extents.insert(std::pair<extent_protocol::extentid_t, struct extentInfo>(1, rootDir));
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
	if (buf.size() > extent_protocol::maxextent)
		return extent_protocol::FBIG; //Exceed the maximun size
	
	std::map<extent_protocol::extentid_t, struct extentInfo>::iterator it;
	it = extents.find(id);
	if (it == extents.end()){ //new extent
		extentInfo newExtent; 
		newExtent.eAttr.size = buf.size();
		newExtent.content = buf;
		time_t curTime = time(NULL);
		newExtent.eAttr.atime = curTime;
		newExtent.eAttr.mtime = curTime;
		newExtent.eAttr.ctime = curTime;

		//All done, insert into our files map
		extents.insert(std::pair<extent_protocol::extentid_t, struct extentInfo>(id, newExtent));

		if (DEBUG){
			std::cout<<"ExtentServer::put : Add new extentid="<<id<<", content="<<newExtent.content<<std::endl;
		}

		return extent_protocol::OK;
	}
	else{
		it->second.eAttr.size = buf.size();
		it->second.content = buf;
		time_t curTime = time(NULL);
		it->second.eAttr.atime = curTime;
		it->second.eAttr.mtime = curTime; 
		it->second.eAttr.ctime = curTime;

		if (DEBUG){
			std::cout<<"ExtentServer::put : Modify extentid="<<id<<", content="<<it->second.content<<std::endl;
		}

		return extent_protocol::OK; 	
	}

	return extent_protocol::IOERR;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
	std::cout<<"Enter extent_server::get"<<std::endl;
	std::map<extent_protocol::extentid_t, struct extentInfo>::iterator it;
	it = extents.find(id);
	
	if (it == extents.end()){ //No such file
		if (DEBUG){
			std::cout<<"ExtentServer::get : No such file!"<<std::endl;
		}
		return extent_protocol::NOENT;
	}
	else{
		buf = it->second.content;
		if (DEBUG){
			std::cout<<"ExtentServer::get : Get extentid="<<id<<", content="<<buf<<std::endl;		
		}
		return extent_protocol::OK;
	}
	
	return extent_protocol::IOERR;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
        // You replace this with a real implementation. We send a phony response
        // for now because it's difficult to get FUSE to do anything (including
        // unmount) if getattr fails.
	if (DEBUG)
		std::cout<<"Enter extent_server::getattr"<<std::endl;
	std::map<extent_protocol::extentid_t, struct extentInfo>::iterator it;
	it = extents.find(id);
	
	if (it == extents.end()){ //No such file
		if (DEBUG){
			std::cout<<"ExtentServer::getattr : No such file!"<<std::endl;
		}
		return extent_protocol::NOENT;
	}
	else{
		a = it->second.eAttr;
		if (DEBUG){
			std::cout<<"ExtentServer::getattr : Getattr extentid="<<id<<std::endl;
			std::cout<<"ExtentServer::getattr : Getattr atime="<<a.atime<<std::endl;
			std::cout<<"ExtentServer::getattr : Getattr size="<<a.size<<std::endl;
			std::cout<<std::endl;		
		}
		return extent_protocol::OK;
	}
	
	return extent_protocol::IOERR;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  return extent_protocol::IOERR;
}

