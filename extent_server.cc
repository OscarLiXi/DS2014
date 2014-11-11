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
	dirInfo rootDir;
	time_t curTime = time(NULL);
	rootDir.dAttr.atime = curTime;
	rootDir.dAttr.mtime = curTime;
	rootDir.dAttr.ctime = curTime;
	rootDir.dAttr.size = 0; // It depends on the contents in this dir, just initilize with 0
	dirs.insert(std::pair<extent_protocol::extentid_t, struct dirInfo>(1, rootDir));
}


int extent_server::put(extent_protocol::extentid_t id, std::string buf, int &)
{
	if (buf.size() > extent_protocol::maxextent)
		return extent_protocol::FBIG; //Exceed the maximun size

  	if (id & IS_FILE){		//It is a file or not 
		std::map<extent_protocol::extentid_t, struct fileInfo>::iterator it;
		it = files.find(id);
		if (it == files.end()){  //New file
			//Define the attributes and contents of new file
			fileInfo newFile;
			newFile.fAttr.size = buf.size();
			newFile.fContent = buf;
			time_t curTime = time(NULL);
			newFile.fAttr.atime = curTime;
			newFile.fAttr.mtime = curTime;
			newFile.fAttr.ctime = curTime;
			
			//All done, insert into our files map
			files.insert(std::pair<extent_protocol::extentid_t, struct fileInfo>(id, newFile));

			//Do we need to modify the size of rootDir?
		}
		else{			//Old file
			//Modify the attrubutes and contents of old file
			it->second.fAttr.size = buf.size();
			it->second.fContent = buf;
			time_t curTime = time(NULL);
			it->second.fAttr.atime = curTime;
			it->second.fAttr.mtime = curTime; //We don't need to modify creation time(ctime)

			//Here, do we need to modify the size of rootDir?
		}	
		return extent_protocol::OK;		
	}
	else{
		std::map<extent_protocol::extentid_t, struct dirInfo>::iterator it;
		it = dirs.find(id);
		if (it == dirs.end()){ //new dir
			//Define the attributes and contents of new file
			dirInfo newDir;
			//newDir.dAttr.size = buf.size(); What is the size of Dir?
			//newDir.dContent = buf; //What is the content of Dir?
			time_t curTime = time(NULL);
			newDir.dAttr.atime = curTime;
			newDir.dAttr.mtime = curTime;
			newDir.dAttr.ctime = curTime;
			
			//All done, insert into our dirs map
			dirs.insert(std::pair<extent_protocol::extentid_t, struct dirInfo>(id, newDir));

			//Do we need to modify the size of rootDir?							
		}
		else{			//old dir
			//it->second.dAttr.size = buf.size(); What is the size of Dir?
			time_t curTime = time(NULL);
			it->second.dAttr.atime = curTime;
			it->second.dAttr.mtime = curTime;
			
			//What is the content of Dir? What is the format of buf? 	
		}
		return extent_protocol::OK;
	}
	return extent_protocol::IOERR;
}

int extent_server::get(extent_protocol::extentid_t id, std::string &buf)
{
  return extent_protocol::IOERR;
}

int extent_server::getattr(extent_protocol::extentid_t id, extent_protocol::attr &a)
{
  // You replace this with a real implementation. We send a phony response
  // for now because it's difficult to get FUSE to do anything (including
  // unmount) if getattr fails.
  a.size = 0;
  a.atime = 0;
  a.mtime = 0;
  a.ctime = 0;
  return extent_protocol::OK;
}

int extent_server::remove(extent_protocol::extentid_t id, int &)
{
  return extent_protocol::IOERR;
}

