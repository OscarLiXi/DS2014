// this is the lock server
// the lock client has a similar interface

#ifndef lock_server_h
#define lock_server_h

#include <pthread.h>
#include <string>
#include <map>
#include "lock_protocol.h"
#include "lock_client.h"
#include "rpc.h"

using namespace std;

class lock_server {

 protected:
	struct lock_t{
		lock_t(){
			pthread_mutex_init(&m,NULL);
			pthread_cond_init(&c,NULL);
			isLocked = false;
		}
		pthread_mutex_t m;
		pthread_cond_t c;
		bool isLocked;
	};
	int nacquire;
	std::map<lock_protocol::lockid_t,lock_server::lock_t> locks;
 public:
  	lock_server();
  	~lock_server();
  	lock_protocol::status stat(int clt, lock_protocol::lockid_t lid, int &);
  	lock_protocol::status acquire(int clt, lock_protocol::lockid_t lid, int &);
  	lock_protocol::status release(int clt, lock_protocol::lockid_t lid, int &);
};

#endif 







