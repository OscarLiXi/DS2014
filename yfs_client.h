#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>
#include <stdio.h>
#include "lock_protocol.h"
#include "lock_client_cache.h"
//implement the class defined in lock_client_cache
//this class is for flush extent cache to the server when lock release happen
class lock_release_user_impl: public lock_release_user{
	private:
		extent_client *ec;
	public: 
		//get the ec object in order to flush
		lock_release_user_impl(extent_client *e){
			ec = e;
			printf("lock_release_use_impl()\n");
		}
		//to flush extent cache, only called when lock release happens
		void dorelease(extent_protocol::extentid_t id){
			printf("yfs-client: do release()\n");
			ec->flush(id);
		}
};

  class yfs_client {
  extent_client *ec;
  lock_client_cache *lc;
  lock_release_user_impl *lu;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG,EXIST };
  typedef int status;

  struct fileinfo {
    unsigned long long size;
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirinfo {
    unsigned long atime;
    unsigned long mtime;
    unsigned long ctime;
  };
  struct dirent {
    std::string name;
    unsigned long long inum;
  };

 private:
  static std::string filename(inum);
  static inum n2i(std::string);
  bool isFileExist(std::string dirContent, std::string name);
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);
  inum ilookup(inum parentID, std::string name);
  inum lookup(inum parentID, std::string name);
  
  int removeFile(inum parentID, std::string fileName); 
  int write(inum fileID, std::string buf,int size,  int off );  
  int setattr(inum fileID, fileinfo fin);
  int getfile(inum fileID, fileinfo &fin);
  int getdir(inum, dirinfo &dirID);
  int getContent(inum inum, std::string &content);
  int create(inum parentID, inum fileID, const char *fileName, inum &ret_inum);

  int getDirContent(inum inum, std::vector<std::pair<std::string, unsigned long long> > &dirContent);

  int read(inum, size_t, off_t, std::string &);

//  void acquire(extent_protocol::extentid_t id);
 // void release(extent_protocol::extentid_t id);
};

#endif 

