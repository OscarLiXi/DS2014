#ifndef yfs_client_h
#define yfs_client_h

#include <string>
//#include "yfs_protocol.h"
#include "extent_client.h"
#include <vector>

#include "lock_protocol.h"
#include "lock_client.h"

  class yfs_client {
  extent_client *ec;
  lock_client *lc;
 public:

  typedef unsigned long long inum;
  enum xxstatus { OK, RPCERR, NOENT, IOERR, FBIG };
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
 public:

  yfs_client(std::string, std::string);

  bool isfile(inum);
  bool isdir(inum);
  inum ilookup(inum parentID, std::string name);
 
  int removeFile(inum parentID, std::string fileName); 
  int write(inum fileID, std::string buf,int size,  int off );  
  int setattr(inum fileID, fileinfo fin);
  int getfile(inum fileID, fileinfo &fin);
  int getdir(inum, dirinfo &dirID);
  int getContent(inum inum, std::string &content);
  int create(inum parentID, inum fileID, const char *fileName);

  int getDirContent(inum inum, std::vector<std::pair<std::string, unsigned long long> > &dirContent);

  int read(inum, size_t, off_t, std::string &);
};

#endif 

