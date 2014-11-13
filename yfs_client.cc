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


/*int yfs_client::getContent(inum inum, std::string &content)
{
	int r = OK;
	if (ec->get(inum, content) != extent_protocol::OK) {
    	r = IOERR;
		goto release;
	}

	release: 
		return r;
}*/
yfs_client::inum yfs_client::ilookup(inum parentID, std::string name)
{
	std::string dirContent;
	if(ec->get(parentID, dirContent) != extent_protocol::OK){
		return 0;
	}	
	//search name in the string dirContent
	std::string::size_type head, tail; //head and tail of id substring in dirContent
	std::string id_str;
	std::string content_cp = std::string(dirContent);
	std::string name_cp = std::string(name);
	content_cp.append(":");
	name_cp.append(":");
	head = content_cp.find(name_cp,0);
	if(head==std::string::npos){
		printf("yfs_client::ilookup(): No matches\n");
		return 0;
	}
	head += name_cp.size();
	tail = content_cp.find(":",head);
	id_str = content_cp.substr(head,tail-head);
	return n2i(id_str);
}
