// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
//#include "extent_client.h"
#include "lock_client_cache.h"
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
  	lu = new lock_release_user_impl(ec);
  	lc = new lock_client_cache(lock_dst,lu);
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
 printf("In yfs_client: getfile() acquire lock %d\n",inum); 
  lc->acquire(inum);
  printf("getfile %016llx\n", inum);
  extent_protocol::attr a;
  lc->acquire(inum);
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

 printf("In yfs_client: getfile() release lock %d\n",inum); 
  lc->release(inum);
  return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
  int r = OK;

 printf("In yfs_client: getdir() acquire lock %d\n",inum); 
 lc->acquire(inum);
  printf("getdir %d\n", inum);
  extent_protocol::attr a;
  lc->acquire(inum);
  if (ec->getattr(inum, a) != extent_protocol::OK) {
    r = IOERR;
    goto release;
  }
  din.atime = a.atime;
  din.mtime = a.mtime;
  din.ctime = a.ctime;

 release:

 printf("In yfs_client: getdir() release lock %d\n",inum); 
  lc->release(inum);
  return r;
}
//return ret_inum as the created file's file identifier
int yfs_client::create(inum parentID, inum inum, const char *name, yfs_client::inum &ret_inum)
{
	
 printf("In yfs_client: create() acquire lock %d\n",parentID); 
	lc->acquire(parentID);
	//std::cout<<"yfs_client::create: bp1"<<std::endl;
	int r = OK;
	std::string dirContent;
	//printf("yfs_client::create():");
	//printf("pid = %d, filename = %s\n",getpid(),name);
	ret_inum = lookup(parentID,name);
	
	if(ret_inum !=0 )
		goto release;
	ret_inum = inum;
	if (ec->get(parentID, dirContent) != extent_protocol::OK) {
		//std::cout<<"yfs_client::create: bp2"<<std::endl;
    	r = IOERR;
		goto release;
	}

 printf("In yfs_client: create() acquire lock %d\n",inum); 
	lc->acquire(inum);
	if(ec->put(inum,std::string()) != extent_protocol::OK){
		r = IOERR;

 printf("In yfs_client: create() release lock %d\n",inum); 
		lc->release(inum);
		goto release;
	}
		
 printf("In yfs_client: create() release lock %d\n",inum); 
	lc->release(inum);
	dirContent.append(name).append(":").append(filename(inum)).append(":");
	std::cout<<"after append yfs_client::create :"<<dirContent<<std::endl;
	//append the new file or dir to its parent in extent server with its name and ID
	if (ec->put(parentID, dirContent) != extent_protocol::OK) {
    		r = IOERR;
		goto release;
	}
	printf("create %s\n",name);
	std::cout << "after create, dirContent = "<< dirContent<< std::endl;
	release:
   		
 printf("In yfs_client: create() release lock %d\n",parentID); 
		lc->release(parentID);
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
	//Dir format: name:ID:name2:ID2: ...
	
 printf("In yfs_client: getDirContent() acquire lock %d\n",inum); 
	lc->acquire(inum);
	int r = OK;
	std::string content;
	std::string tempName;
	size_t contentLength;
	int begin = 0, end = 0;
	int cycleTime = 0;
	std::string symbol (":");
	
	lc->acquire(inum);

	if (ec->get(inum, content) != extent_protocol::OK) {
		//std::cout<<"yfs_client::create: bp2"<<std::endl;
    		r = IOERR;
		lc->release(inum);
		return r;
	}
	//Now we get the content of dir, and separate into names and ids
	contentLength = content.size();
	

	if (content.find(symbol) == std::string::npos)
	{
		lc->release(inum);
		return r;
	}
	std::cout<<"content: "<<content<<std::endl;
	while(end < contentLength){
		size_t pos = content.find(symbol, begin);
		if (pos == std::string::npos)
			break; // the end of string

		end = pos;
		cycleTime ++;

		if (cycleTime % 2 == 1) //Store the name
			tempName = content.substr(begin, end - begin);
		else{
			unsigned long long id = n2i(content.substr(begin, end - begin));
			std::cout<<"id: "<<id<<std::endl;
			dirContent.push_back(std::make_pair(tempName, id));
		}

		begin = end + 1; //Find next substring		
	}

	//Find last string, because we did not use : to end it. modify the format, now these codes are useless. R.I.P
	//unsigned long long id = n2i(content.substr(begin, end));
	//dirContent.push_back(std::make_pair(tempName, id));
 printf("In yfs_client: getDirContent() release lock %d\n",inum); 
	lc->release(inum);
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

int yfs_client::read(inum inum, size_t readSize, off_t offset, std::string &retString)
{
	printf("Enter yfs_Client::read\n");

 printf("In yfs_client: read() acquire lock %d\n",inum); 
	lc->acquire(inum);
	//printf("readSize: %d, offset: %d\n", readSize, offset);
	int r = OK;
	std::string content;
	size_t length;
	if (ec->get(inum, content) != extent_protocol::OK) {
    	r = IOERR;
		goto release;
		return r;
	}
	length = content.size();
	

	length = content.size();
	//printf("length: %d\n", length);
	//readSize could be larger than length
	if (readSize < 0 || offset < 0){
		printf("yfs_Client::read : size or offset error\n");
		r = IOERR;
		goto release;
		return r;
	}

	if (offset >= length){ // return "\0"
		retString = "";
		std::cout<<"read content = "<<retString<<std::endl;
		goto release;
		return r;				
	}
	
	retString = content.substr(offset, readSize);
	//std::cout<<"read content = "<<retString<<std::endl;
release:
 printf("In yfs_client: read() release lock %d\n",inum); 
	lc->release(inum);
	return r;
}

bool yfs_client::isFileExist(std::string dirContent, std::string name)
{
	
	std::string::size_type head; //head and tail of id substring in dirContent
	std::string content_cp = std::string(dirContent);
	std::string name_cp = std::string(name);
	//content_cp.append(":");
	name_cp.append(":");
	head = content_cp.find(name_cp,0);
	if(head==std::string::npos)
		return false;
	else
		return true;
}

yfs_client::inum yfs_client::lookup(yfs_client::inum parentID, std::string name)
{
	std::string dirContent;
	if(ec->get(parentID, dirContent) != extent_protocol::OK){
		return 0;
	}	
	std::cout<<"yfs_client::lookup : parentID:"<<parentID<<std::endl;
	std::cout<<"yfs_client::lookup : dirContent:"<<dirContent<<std::endl;
	//search name in the string dirContent
	std::string::size_type head, tail; //head and tail of id substring in dirContent
	std::string id_str;
	std::string content_cp = std::string(dirContent);
	std::string name_cp = std::string(name);
	//content_cp.append(":");
	name_cp.append(":");
	head = content_cp.find(name_cp,0);
	if(head==std::string::npos){
		printf("yfs_client::lookup(): No matches\n");
		return 0;
	}
	head += name_cp.size();
	tail = content_cp.find(":",head);
	id_str = content_cp.substr(head,tail-head);
	return n2i(id_str);
}

yfs_client::inum yfs_client::lookup(inum parentID, std::string name)
{
	
	printf("In yfs_client: ilookup() acquire lock %d\n",parentID);
	lc->acquire(parentID);
	std::string dirContent;
	std::string id_str;
	//lc->acquire(parentID);

	//search name in the string dirContent
	std::string::size_type head, tail; //head and tail of id substring in dirContent
	
	std::string content_cp;// = std::string(dirContent);
	std::string name_cp;// = std::string(name);

	if(ec->get(parentID, dirContent) != extent_protocol::OK){
	printf("In yfs_client: ilookup() release lock %d\n",parentID);
			lc->release(parentID);
		return 0;
	}	
	std::cout<<"yfs_client::ilookup : parentID:"<<parentID<<std::endl;
	std::cout<<"yfs_client::ilookup : dirContent:"<<dirContent<<std::endl;
	content_cp = std::string(dirContent);
	name_cp = std::string(name);
	//content_cp.append(":");
	name_cp.append(":");
	head = content_cp.find(name_cp,0);
	if(head==std::string::npos){
		printf("yfs_client::ilookup(): No matches\n");
		goto release;
	}
	head += name_cp.size();
	tail = content_cp.find(":",head);
	id_str = content_cp.substr(head,tail-head);

release:
	//lc->release(parentID);
	return n2i(id_str);
}

yfs_client::inum yfs_client::ilookup(inum parentID, std::string name)
{
	std::string dirContent;
	std::string id_str;
	lc->acquire(parentID);

	//search name in the string dirContent
	std::string::size_type head, tail; //head and tail of id substring in dirContent
	
	std::string content_cp;// = std::string(dirContent);
	std::string name_cp;// = std::string(name);

	if(ec->get(parentID, dirContent) != extent_protocol::OK){
		goto release;
	}	
	std::cout<<"yfs_client::ilookup : parentID:"<<parentID<<std::endl;
	std::cout<<"yfs_client::ilookup : dirContent:"<<dirContent<<std::endl;
	content_cp = std::string(dirContent);
	name_cp = std::string(name);
	//content_cp.append(":");
	name_cp.append(":");
	head = content_cp.find(name_cp,0);
	if(head==std::string::npos){

	printf("In yfs_client: ilookup() release lock %d\n",parentID);
		printf("yfs_client::ilookup(): No matches\n");
		lc->release(parentID);
		return 0;
	}
	head += name_cp.size();
	tail = content_cp.find(":",head);
	id_str = content_cp.substr(head,tail-head);
	printf("In yfs_client: ilookup() release lock %d\n",parentID);
	lc->release(parentID);
	return n2i(id_str);
}

int yfs_client::setattr(inum fileID, fileinfo fin)
{
	
	printf("In yfs_client: setattr() acquire lock %d\n",fileID);
		lc->acquire(fileID);	
	int r = OK;

  	printf("setattr %016llx\n", fileID);
	lc->acquire(fileID);
  	extent_protocol::attr a;
  	a.size = fin.size;
  	if (ec->setattr(fileID, a) != extent_protocol::OK) {
    	r = IOERR;
    	goto release;
  	}
 release:

	printf("In yfs_client: setattr() release lock %d\n",fileID);
	lc->release(fileID);
	return r;	
	
}

int yfs_client::write(inum fileID, std::string buf, int size, int off)
{
	
	printf("In yfs_client: write() acquire lock %d\n",fileID);
	lc->acquire(fileID);
	int r = OK, content_size;
	std::string content;
	lc->acquire(fileID);
	if(ec->get(fileID,content) != extent_protocol::OK){
		r = IOERR;
		goto release;
	}
	
	//std::cout << "yfs_client::write():before insert, content=" << content << std::endl;
	//std::cout << "yfs_client::write(): off=" << off << "buf= " << buf << std::endl;
	content_size = content.size();
	if(off >= content_size )
		content.append(buf.substr(0, size));
	else	
		content.replace(off,size,  buf.substr(0,size));
	//std::cout << "yfs_client::write(): after insert, content=" << content << std::endl;
	if(ec->put(fileID,content) != extent_protocol::OK){
		r = IOERR;
		goto release;
	}		
release:

	printf("In yfs_client: write() release lock %d\n",fileID);
	lc->release(fileID);
	return r;
}

int yfs_client::removeFile(inum parentID, std::string fileName)
{
	int r = OK;
	
	printf("In yfs_client: remove() acquire lock %d\n",parentID);
	lc->acquire(parentID);

	std::string dirContent, content_cp, name_cp, id_str;
	std::string::size_type head,tail,head2;
	inum fileID;
	if(ec->get(parentID, dirContent) != extent_protocol::OK){
		r = IOERR;
		goto release;
	}
	//search file in dirContent
	content_cp = std::string(dirContent);
	name_cp = std::string(fileName);
	//content_cp.append(":");
	name_cp.append(":");
	head = content_cp.find(name_cp,0);
	printf("!!!!!!!!before remove");
	printf("yfs_client::remove():");
	std::cout << "dirContent: "<< dirContent << std::endl;
	std::cout << "file_name: " << fileName << std::endl;	
	if(head==std::string::npos){
		printf("yfs_client::removeFile(): No such file\n");
		r = NOENT;
		goto release;
	}
	head2 = head + name_cp.size();
	tail = content_cp.find(":",head2);
	id_str = content_cp.substr(head2,tail-head2);
	fileID = n2i(id_str);
	
	//remove it from dirContent
	//check if it is the last id in the string
	dirContent.erase(head,tail-head+1);
	
	printf("!!!!!!!!after remove");
	printf("yfs_client::remove():");
	std::cout << "dirContent: "<< dirContent << std::endl;

	printf("In yfs_client: remove() acquire lock %d\n",fileID);
	lc->acquire(fileID);	
	//remove it from extent_server
	if(ec->remove(fileID) != extent_protocol::OK){
		r = IOERR;

	printf("In yfs_client: remove() release lock %d\n",fileID);
		lc->release(fileID);
		goto release;
	}
	
	printf("In yfs_client: remove() release lock %d\n",fileID);
	lc->release(fileID);
	//modify the directory content in extent_server	
	if(ec->put(parentID,dirContent) != extent_protocol::OK){
		r = IOERR;
		goto release;
	}
release:

	printf("In yfs_client: remove() release lock %d\n",parentID);
	lc->release(parentID);
	return r;
}

/*void yfs_client::acquire(extent_protocol::extentid_t id)
{
	lc->acquire(id);
}

void yfs_client::release(extent_protocol::extentid_t id)
{
	lc->release(id);
}*/


