// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);

}

yfs_client::inum
yfs_client::n2i(std::string n)
{
  std::istringstream ist(n);
  unsigned long long finum;
  ist >> finum;
  return finum;
}

std::string
yfs_client::filename(inum inum)
{
  std::ostringstream ost;
  ost << inum;
  return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
  if(inum & 0x80000000)
    return true;
  return false;
}

bool
yfs_client::isdir(inum inum)
{
  return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
  int r = OK;


  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }

  fin.atime = a.atime;
  fin.mtime = a.mtime;
  fin.ctime = a.ctime;
  fin.size = a.size;
  printf("getfile %016llx -> sz %llu\n", inum, fin.size);

 release:

  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;


  printf("getdir %016llx\n", inum);
  extent_protocol::attr a;
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:
  return r;
}

int yfs_client::create(inum parentID, inum inum, const char *name)
{
	std::cout<<"yfs_client::create: bp1"<<std::endl;
	int r = OK;
	std::string dirContent;	
	if (ec->get(parentID, dirContent) != extent_protocol::OK) {
		std::cout<<"yfs_client::create: bp2"<<std::endl;
    		r = IOERR;
		goto release;
	}
		std::cout<<"yfs_client::create: bp3"<<std::endl;
	//if it is a file, create a new file in extent server 
	if(isfile(inum)){
		if(ec->put(inum,std::string()) != extent_protocol::OK){
			r = IOERR;
			goto release;
		}	
	}
	if(!dirContent.empty())
		dirContent.append(":");
	dirContent.append(name).append(":").append(filename(inum));
	//append the new file or dir to its parent in extent server with its name and ID
	if (ec->put(parentID, dirContent) != extent_protocol::OK) {
    	r = IOERR;
		goto release;
	}
	
	release: 
		return r;
}

unsigned long long stringToid(std::string s) 
{
	int len = s.length();
	unsigned long long ret = 0;
	for (int i = 0; i < len; ++i) {
		ret = ret * 10 + s.at(i) - '0';
	}
	return ret;
}

int yfs_client::getDirContent(inum inum, std::vector<std::pair<std::string, unsigned long long> > &dirContent)
{
	//Dir format: name:ID:name2:ID2 ...
	int r = OK;
	std::string content;	
	if (ec->get(inum, content) != extent_protocol::OK) {
		std::cout<<"yfs_client::create: bp2"<<std::endl;
    		r = IOERR;
		return r;
	}

	//Now we get the content of dir, and separate into names and ids
	size_t contentLength = content.size();
	int begin = 0, end = 0;
	int cycleTime = 0;
	std::string symbol (":");
	std::string tempName;

	while(end < contentLength){
		size_t pos = content.find(symbol, begin);
		if (pos == std::string::npos)
			break; // the end of string

		end = pos;
		cycleTime ++;

		if (cycleTime % 2 == 1) //Store the name
			tempName = content.substr(begin, end - begin);
		else{
			//std::cout<<"idstring: "<<end<<std::endl;
			unsigned long long id = stringToid(content.substr(begin, end - begin));
			dirContent.push_back(std::make_pair(tempName, id));
		}

		begin = end + 1; //Find next substring		
	}

	//Find last string, because we did not use : to end it
	unsigned long long id = stringToid(content.substr(begin, end));
	dirContent.push_back(std::make_pair(tempName, id));

	return r;
}
