/*
 * receive request from fuse and call methods of yfs_client
 *
 * started life as low-level example in the fuse distribution.  we
 * have to use low-level interface in order to get i-numbers.  the
 * high-level interface only gives us complete paths.
 */

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <arpa/inet.h>
#include "yfs_client.h"

#define DEBUG 0

int myid;
yfs_client *yfs;

int id() { 
  return myid;									
}

yfs_client::status
getattr(yfs_client::inum inum, struct stat &st)
{
  yfs_client::status ret;

  bzero(&st, sizeof(st));

  st.st_ino = inum;

  printf("getattr %016llx %d\n", inum, yfs->isfile(inum));
	
  if(yfs->isfile(inum)){
     yfs_client::fileinfo info;
     ret = yfs->getfile(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFREG | 0666;
     st.st_nlink = 1;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     st.st_size = info.size;
     if (DEBUG)
     	printf("   getattr -> %llu\n", info.size);
   } else {
     yfs_client::dirinfo info;
     ret = yfs->getdir(inum, info);
     if(ret != yfs_client::OK)
       return ret;
     st.st_mode = S_IFDIR | 0777;
     st.st_nlink = 2;
     st.st_atime = info.atime;
     st.st_mtime = info.mtime;
     st.st_ctime = info.ctime;
     if (DEBUG)
     	printf("   getattr -> %lu %lu %lu\n", info.atime, info.mtime, info.ctime);
   }
   return yfs_client::OK;
}


void
fuseserver_getattr(fuse_req_t req, fuse_ino_t ino,
          struct fuse_file_info *fi)
{
    struct stat st;
    yfs_client::inum inum = ino; // req->in.h.nodeid;
    yfs_client::status ret;

    ret = getattr(inum, st);
    if(ret != yfs_client::OK){
      fuse_reply_err(req, ENOENT);
      return;
    }
    fuse_reply_attr(req, &st, 0);
}

void
fuseserver_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)
{
	
	yfs_client::status ret;
	if (DEBUG)
		printf("fuseserver_setattr 0x%x\n", to_set);
	//only consider to change the size attribute
  	if (FUSE_SET_ATTR_SIZE & to_set) {
    	
		printf("   fuseserver_setattr set size to %zu\n", attr->st_size);
    	struct stat st;
		
		yfs_client::fileinfo info;
		info.size = attr->st_size;
    	ret = yfs->setattr(ino,info);		
		
		if(ret != yfs_client::OK){
			fuse_reply_err(req, ENOSYS);
			return;
		}
		
		ret = yfs->getfile(ino, info);
		if(ret != yfs_client::OK){
			fuse_reply_err(req, ENOSYS);
			return;
		}

		st.st_mode = S_IFREG | 0666;
		st.st_nlink = 1;
		st.st_atime = info.atime;
		st.st_mtime = info.mtime;
		st.st_ctime = info.ctime;
		st.st_size = info.size;
    	fuse_reply_attr(req, &st, 0);
  	}
  	else{
		fuse_reply_err(req, ENOSYS);
  	}
}

void
fuseserver_read(fuse_req_t req, fuse_ino_t ino, size_t size,
      off_t off, struct fuse_file_info *fi)
{
	if (DEBUG)
		std::cout<<"Enter fuseserver_read."<<std::endl;
	yfs_client::status ret;
	std::string retString;
    	ret = yfs->read(ino, size, off, retString);		
	if(ret != yfs_client::OK){
		fuse_reply_err(req, ENOSYS);
		return;
	}
	size = retString.size();
	char* buf = new char [size];
	strcpy(buf, retString.c_str()); //copy read content to buf

	fuse_reply_buf(req, buf, size);
	//delete[] buf;
  // You fill this in
//#if 1
//  fuse_reply_buf(req, buf, size);
//#else
//  fuse_reply_err(req, ENOSYS);
//#endif
}

void
fuseserver_write(fuse_req_t req, fuse_ino_t ino,
  const char *buf, size_t size, off_t off,
  struct fuse_file_info *fi)
{
  	// You fill this in
	yfs_client::status ret;
	printf("fuse.cc: write %s \n", buf);	
	ret = yfs->write(ino,buf,off);
	if(ret != yfs_client::OK){
		fuse_reply_err(req, ENOSYS);
		return;
	}
 
	fuse_reply_write(req, size);

}

yfs_client::status
fuseserver_createhelper(fuse_ino_t parent, const char *name,
     mode_t mode, struct fuse_entry_param *e)
{
	
  	// You fill this in
  	yfs_client::status ret;
	yfs_client::inum fileID = random() | 0x80000000;
	ret = yfs->create(parent,fileID,name); 
	if (DEBUG)
		std::cout<<"createhelper: bp1"<<std::endl;
	if(ret != yfs_client::OK)	
		return ret;
	yfs_client::fileinfo info;
	ret = yfs->getfile(fileID,info);
	if(ret != yfs_client::OK)	
		return ret;
	//reply the attribute of the file/dir to FUSE	
	e->ino = fileID;
	e->attr.st_ino = fileID;
	e->attr.st_mode = S_IFREG | 0666;
	e->attr.st_nlink = 1;
	e->attr.st_atime = info.atime;
	e->attr.st_mtime = info.mtime;
	e->attr.st_ctime = info.ctime;
	e->attr.st_size = info.size;
	
	return yfs_client::OK;
}

void
fuseserver_create(fuse_req_t req, fuse_ino_t parent, const char *name,
   mode_t mode, struct fuse_file_info *fi)
{
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    if (DEBUG)
    	std::cout<<"fuseserver_create: bp1"<<std::endl;
    fuse_reply_create(req, &e, fi);
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void fuseserver_mknod( fuse_req_t req, fuse_ino_t parent, 
    const char *name, mode_t mode, dev_t rdev ) {
  struct fuse_entry_param e;
  if( fuseserver_createhelper( parent, name, mode, &e ) == yfs_client::OK ) {
    if (DEBUG)
    	std::cout<<"fuseserver_mknod: bp1"<<std::endl;
    fuse_reply_entry(req, &e);
    //if (DEBUG)
    //	std::cout<<"fuseserver_mknod: bp2"<<std::endl;
  } else {
    fuse_reply_err(req, ENOENT);
  }
}

void
fuseserver_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
  	struct fuse_entry_param e;
  	e.attr_timeout = 0.0;
  	e.entry_timeout = 0.0;

  	// You fill this in:
  	// Look up the file named `name' in the directory referred to by
  	// `parent' in YFS. If the file was found, initialize e.ino and
  	// e.attr appropriately.
	yfs_client::inum inum;
	inum = yfs->ilookup(parent,name);
	if(inum==0){
		printf("In fuse.cc lookup(): %s no found\n",name);
		fuse_reply_err(req,ENOENT);
		return;
	}
	//if it is a file
	if(yfs->isfile(inum)){
     	yfs_client::fileinfo info;
     	if(yfs->getfile(inum, info) != yfs_client::OK){
			printf("In fuse.cc lookup(): getfile failed\n");
			fuse_reply_err(req,ENOENT);
		}
		e.ino = inum;
     	e.attr.st_mode = S_IFREG | 0666;
     	e.attr.st_nlink = 1;
     	e.attr.st_atime = info.atime;
     	e.attr.st_mtime = info.mtime;
     	e.attr.st_ctime = info.ctime;
     	e.attr.st_size = info.size;
   	} 
	//if it is a directory
	else {
     	yfs_client::dirinfo info;
     	if(yfs->getdir(inum, info) != yfs_client::OK){
			printf("In fuse.cc lookup(): getdir failed\n");
			fuse_reply_err(req,ENOENT);
		}
		e.ino = inum;
     	e.attr.st_mode = S_IFDIR | 0777;
     	e.attr.st_nlink = 2;
     	e.attr.st_atime = info.atime;
     	e.attr.st_mtime = info.mtime;
     	e.attr.st_ctime = info.ctime;
   	}
	fuse_reply_entry(req, &e);		
}


struct dirbuf {
    char *p;
    size_t size;
};

void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino)
{
    struct stat stbuf;
    size_t oldsize = b->size;
    b->size += fuse_dirent_size(strlen(name));
    b->p = (char *) realloc(b->p, b->size);
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = ino;
    fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
          off_t off, size_t maxsize)
{
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

void
fuseserver_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
          off_t off, struct fuse_file_info *fi)
{
  //std::cout<<"fuseserver_readdir: bp1"<<std::endl;
  yfs_client::inum inum = ino; // req->in.h.nodeid;
  struct dirbuf b;
  yfs_client::dirent e;

  printf("fuseserver_readdir\n");

 if(!yfs->isdir(inum)){
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  memset(&b, 0, sizeof(b));


   // fill in the b data structure using dirbuf_add
	yfs_client::status ret;
	std::vector<std::pair<std::string, unsigned long long> > dirContent;
	ret = yfs->getDirContent(ino, dirContent);
	if (ret != yfs_client::OK){
		return;
	}

	while(!dirContent.empty())
	{
		std::pair<std::string, unsigned long long> output = dirContent.back();
		dirContent.pop_back();
		dirbuf_add(&b, output.first.c_str(), output.second);
		//std::cout<<"Name: "<<output.first<<std::endl;
		//std::cout<<"Id: "<<output.second<<std::endl;
		//std::cout<<std::endl;
	}

   reply_buf_limited(req, b.p, b.size, off, size);
   free(b.p);
 }


void
fuseserver_open(fuse_req_t req, fuse_ino_t ino,
     struct fuse_file_info *fi)
{
  	// check if it is a file
	if(yfs->isdir(ino))  
  		fuse_reply_err(req, ENOSYS);
  	fuse_reply_open(req, fi);

}

void
fuseserver_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
     mode_t mode)
{
	struct fuse_entry_param e;
  	// You fill this in
	yfs_client::status ret;
	//RAND_MAX=7FFFFFFF
	yfs_client::inum dirID = random(); 
	ret = yfs->create(parent,dirID,name);
	if(ret != yfs_client::OK){
		fuse_reply_err(req, ENOSYS);
		return;
	}
	yfs_client::fileinfo info;
	ret = yfs->getfile(dirID,info);
	if(ret != yfs_client::OK){
		fuse_reply_err(req, ENOSYS);
		return;
	}
	e.ino = dirID;
	e.attr.st_ino = dirID;
	e.attr.st_mode = S_IFDIR | 0777;
	e.attr.st_nlink = 2;
	e.attr.st_atime = info.atime;
	e.attr.st_mtime = info.mtime;
	e.attr.st_ctime = info.ctime;
	
  	fuse_reply_entry(req, &e);
}

void
fuseserver_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{

  // You fill this in
  // Success:	fuse_reply_err(req, 0);
  // Not found:	fuse_reply_err(req, ENOENT);
  fuse_reply_err(req, ENOSYS);
}

void
fuseserver_statfs(fuse_req_t req)
{
  struct statvfs buf;

  printf("statfs\n");

  memset(&buf, 0, sizeof(buf));

  buf.f_namemax = 255;
  buf.f_bsize = 512;

  fuse_reply_statfs(req, &buf);
}

struct fuse_lowlevel_ops fuseserver_oper;

int
main(int argc, char *argv[])
{
  char *mountpoint = 0;
  int err = -1;
  int fd;

  setvbuf(stdout, NULL, _IONBF, 0);

  if(argc != 4){
    fprintf(stderr, "Usage: yfs_client <mountpoint> <port-extent-server> <port-lock-server>\n");
    exit(1);
  }
  mountpoint = argv[1];

  srandom(getpid());

  myid = random();

  yfs = new yfs_client(argv[2], argv[3]);

  fuseserver_oper.getattr    = fuseserver_getattr;
  fuseserver_oper.statfs     = fuseserver_statfs;
  fuseserver_oper.readdir    = fuseserver_readdir;
  fuseserver_oper.lookup     = fuseserver_lookup;
  fuseserver_oper.create     = fuseserver_create;
  fuseserver_oper.mknod      = fuseserver_mknod;
  fuseserver_oper.open       = fuseserver_open;
  fuseserver_oper.read       = fuseserver_read;
  fuseserver_oper.write      = fuseserver_write;
  fuseserver_oper.setattr    = fuseserver_setattr;
  fuseserver_oper.unlink     = fuseserver_unlink;
  fuseserver_oper.mkdir      = fuseserver_mkdir;

  const char *fuse_argv[20];
  int fuse_argc = 0;
  fuse_argv[fuse_argc++] = argv[0];
#ifdef __APPLE__
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "nolocalcaches"; // no dir entry caching
  fuse_argv[fuse_argc++] = "-o";
  fuse_argv[fuse_argc++] = "daemon_timeout=86400";
#endif

  // everyone can play, why not?
  //fuse_argv[fuse_argc++] = "-o";
  //fuse_argv[fuse_argc++] = "allow_other";

  fuse_argv[fuse_argc++] = mountpoint;
  fuse_argv[fuse_argc++] = "-d";

  fuse_args args = FUSE_ARGS_INIT( fuse_argc, (char **) fuse_argv );
  int foreground;
  int res = fuse_parse_cmdline( &args, &mountpoint, 0 /*multithreaded*/, 
        &foreground );
  if( res == -1 ) {
    fprintf(stderr, "fuse_parse_cmdline failed\n");
    return 0;
  }
  
  args.allocated = 0;

  fd = fuse_mount(mountpoint, &args);
  if(fd == -1){
    fprintf(stderr, "fuse_mount failed\n");
    exit(1);
  }

  struct fuse_session *se;

  se = fuse_lowlevel_new(&args, &fuseserver_oper, sizeof(fuseserver_oper),
       NULL);
  if(se == 0){
    fprintf(stderr, "fuse_lowlevel_new failed\n");
    exit(1);
  }

  struct fuse_chan *ch = fuse_kern_chan_new(fd);
  if (ch == NULL) {
    fprintf(stderr, "fuse_kern_chan_new failed\n");
    exit(1);
  }

  fuse_session_add_chan(se, ch);
  // err = fuse_session_loop_mt(se);   // FK: wheelfs does this; why?
  err = fuse_session_loop(se);
    
  fuse_session_destroy(se);
  close(fd);
  fuse_unmount(mountpoint);

  return err ? 1 : 0;
}

