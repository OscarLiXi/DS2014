#ifndef lock_server_cache_h
#define lock_server_cache_h

#include <string>
#include "lock_protocol.h"
#include "rpc.h"
#include "lock_server.h"
#include "rsm.h"
struct retryMsg{
	lock_protocol::lockid_t lid;
	std::string clientId;
	int seqNum;
};

class lock_server_cache : public lock_server {
 private:
  class rsm *rsm;
  std::map<lock_protocol::lockid_t,std::string> lockToClient;
  std::map<std::string, rpcc*> clientToRpcc;
  std::list<lock_protocol::lockid_t> revokeList;
  std::list<lock_protocol::lockid_t> retryList;
  std::vector<retryMsg> waitList; //Could be multiple lock <-> multiple clients
  std::map<std::pair<lock_protocol::lockid_t,std::string>, int> seqNumMap; //Sequence number per client per lock
  pthread_mutex_t stateLock;
  pthread_mutex_t holderLock; // lock when access lockToClient
  pthread_mutex_t waitListLock; //lock to waitList
  pthread_mutex_t rpccLock; //lock to revoke and retry rpcc
  pthread_mutex_t seqnumLock; //lock for sequence number
  lock_t revokeLock;
  lock_t retryLock;
 public:
  lock_server_cache();
  lock_server_cache(class rsm *rsm = 0);
  ~lock_server_cache();

  //lock_protocol::status stat(std::string, lock_protocol::lockid_t, int &);
  lock_protocol::status stat(lock_protocol::lockid_t, int &);
  lock_protocol::status acquire(std::string, int, lock_protocol::lockid_t , int &);
  lock_protocol::status release(std::string, int, lock_protocol::lockid_t , int &);
  void revoker();
  void retryer();
};

#endif
