// RPC stubs for clients to talk to lock_server, and cache the locks
// see lock_client.cache.h for protocol details.

#include "lock_client_cache.h"
#include "rpc.h"
#include <sstream>
#include <iostream>
#include <stdio.h>


static void *
releasethread(void *x)
{
  lock_client_cache *cc = (lock_client_cache *) x;
  cc->releaser();
  return 0;
}

int lock_client_cache::last_port = 0;

lock_client_cache::lock_client_cache(std::string xdst, 
				     class lock_release_user *_lu)
  : lock_client(xdst), lu(_lu)
{

	//Create a new rpc client

	//sockaddr_in dstsock;
  	//make_sockaddr(xdst.c_str(), &dstsock);
  	//cl = new rpcc(dstsock);

	//for lab 8
	rsm_cl = new rsm_client(xdst);
  	//if (cl->bind() < 0) {
    	//	printf("lock_client: call bind\n");
  	//}

	assert(pthread_mutex_init(&lockStatusLock,0)==0);
	assert(pthread_mutex_init(&lockSeqLock,0)==0);
	assert(pthread_mutex_init(&seqLock,0)==0);
	seqNum = 0;

  srand(time(NULL)^last_port);
  rlock_port = ((rand()%32000) | (0x1 << 10));
  const char *hname;
  // assert(gethostname(hname, 100) == 0);
  hname = "127.0.0.1";
  std::ostringstream host;
  host << hname << ":" << rlock_port;
  id = host.str();
  last_port = rlock_port;
  rpcs *rlsrpc = new rpcs(rlock_port);

  /* register RPC handlers with rlsrpc */
  rlsrpc->reg(rlock_protocol::revoke, this, &lock_client_cache::revoke);
  rlsrpc->reg(rlock_protocol::retry, this, &lock_client_cache::retry); //How to free rlsrpc?

  pthread_t th;
  int r = pthread_create(&th, NULL, &releasethread, (void *) this);
  assert (r == 0);
}

lock_client_cache::~lock_client_cache() 
{
	delete rsm_cl;
}


void
lock_client_cache::releaser()
{

  // This method should be a continuous loop, waiting to be notified of
  // freed locks that have been revoked by the server, so that it can
  // send a release RPC.

	while(1){
		lock_protocol::lockid_t lid;
		
		pthread_mutex_lock(&releaseLock.m);
		//if (!releaseList.empty())
		//	std::cout<<"release list is not empty!"<<std::endl;
		while(releaseList.empty()){
			pthread_cond_wait(&releaseLock.c, &releaseLock.m);
		}
		pthread_mutex_unlock(&releaseLock.m);

		std::list<lock_protocol::lockid_t>::iterator it;
		std::cout<<"client lock1"<<std::endl;
		pthread_mutex_lock(&clientLock.m);
		std::cout<<"client lock2"<<std::endl;
		for (it = releaseList.begin(); it != releaseList.end(); ){
			
			lid = *it;
			//std::cout<<"LockClient: Releaser: releaseList Size: "<<releaseList.size()<<std::endl;
			int tempseqNum = -1;
			int statusNum = -1;
			pthread_mutex_lock(&lockSeqLock);
			if (lockSeqMap.find(lid) == lockSeqMap.end()){
				std::cout<<"LockClient: Releaser: can not find seqNum for lock: "<<lid<<std::endl;
				return;
			}
			else
				tempseqNum = lockSeqMap[lid];
			pthread_mutex_unlock(&lockSeqLock);
			pthread_mutex_lock(&lockStatusLock);
			if (lockStatusMap.find(lid) == lockStatusMap.end()){
				std::cout<<"LockClient: Releaser: can not find status for lock: "<<lid<<std::endl;
				return;
			}
			else
				statusNum = lockStatusMap[lid];
			printf("LockClient: Releaser: lockid=%d, lock status=%d\n",lid,statusNum);	
			pthread_mutex_unlock(&lockStatusLock);

			if (statusNum == Free || statusNum == Releasing){
				//std::cout<<"releaser: statusNum: "<<statusNum<<std::endl;
				//std::cout<<"releaser: statusNum2: "<<statusNum<<std::endl;
				int r;
				//calling extent_client to flush extents cache before lock release
				lu->dorelease(lid);
				lock_protocol::status response = rsm_cl->call(lock_protocol::release, id, tempseqNum, lid, r);
				
				if (response != lock_protocol::OK){
					std::cout<<"Release failed!"<<std::endl;
					return;
				}

				std::cout<<"LockClient: Releaser: client: "<<id<<" release lock: "<<lid<<" with seqNum: "<<tempseqNum<<std::endl;
				pthread_mutex_lock(&releaseLock.m);
				it = releaseList.erase(it);
				pthread_mutex_unlock(&releaseLock.m);
				pthread_mutex_lock(&lockStatusLock);
				lockStatusMap[lid] = None;
				pthread_mutex_unlock(&lockStatusLock);

				
			}
			else if (statusNum == None){ //already released
				pthread_mutex_lock(&releaseLock.m);
				it = releaseList.erase(it);
				pthread_mutex_unlock(&releaseLock.m);
			}
			else{ //still locked
				//std::cout<<"LockClient: Releaser: StatusNum:"<<statusNum<<std::endl;
				it++;
			}
			//pthread_mutex_unlock(&lockStatusLock);
			//std::cout<<"bp"<<std::endl;
		}
		
		pthread_cond_broadcast(&clientLock.c); //wake up all threads tell them to reacquire the lock
		pthread_mutex_unlock(&clientLock.m);	
	}
}

int 
lock_client_cache::isInList(std::list<lock_protocol::lockid_t> array, lock_protocol::lockid_t lid)
{
	std::list<lock_protocol::lockid_t>::iterator l;
	int flag = 0;
	for ( l = array.begin() ; l != array.end(); l ++ ){
		if ( *l == lid )
		{	
			flag = 1;
			break;
		}
	}	
	return flag;
}

lock_protocol::status
lock_client_cache::acquire(lock_protocol::lockid_t lid)
{
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&clientLock.m); //if everything is alright, we might not need this lock

	//When a thread try to acquire a lock, first we check if this lock is already handed by its own client
	pthread_mutex_lock(&lockStatusLock);
	std::map<lock_protocol::lockid_t, int>::iterator it;
	it = lockStatusMap.find(lid);
	pthread_mutex_unlock(&lockStatusLock);	
	if (it == lockStatusMap.end() || it->second == None){ //client knows nothing about this lock
		pthread_mutex_lock(&lockStatusLock);
		lockStatusMap[lid] = None;
		pthread_mutex_unlock(&lockStatusLock);	

		acquire:
			pthread_mutex_lock(&lockStatusLock);
			lockStatusMap[lid] = Acquiring; // change the status to acquiring
			pthread_mutex_unlock(&lockStatusLock);
			//pthread_mutex_unlock(&clientLock.m);
			int tempSeq;
			pthread_mutex_lock(&seqLock);
			seqNum ++;
			tempSeq = seqNum;
			pthread_mutex_unlock(&seqLock);
			while (1){
				int r;
				lock_protocol::status response = rsm_cl->call(lock_protocol::acquire, id, tempSeq, lid, r);
				std::cout<<"Client: "<<id<<" acquire lock: "<<lid<<" with seqNum: "<<tempSeq<<std::endl;
				if (response == lock_protocol::OK){ //Server granted lock
					
					pthread_mutex_lock(&lockStatusLock);
					lockStatusMap[lid] = Locked;
					pthread_mutex_unlock(&lockStatusLock);
					pthread_mutex_lock(&lockSeqLock);
					lockSeqMap[lid] = tempSeq; // store it and send it with releasing
					pthread_mutex_unlock(&lockSeqLock);
					goto RELEASE_LOCK;
				}
				else if (response == lock_protocol::RETRY){ //FIXME: what if RPCERR?
					pthread_mutex_unlock(&clientLock.m);
					pthread_mutex_lock(&lockStatusLock);
					lockStatusMap[lid] = None;
					pthread_mutex_unlock(&lockStatusLock);
					pthread_mutex_lock(&retryLock.m);
					//first we find if this lock is in our retryList
					std::cout<<"LockClient: client: "<<id<<" is waiting to retry lock:"<<lid<<std::endl;
					while(!isInList(retryList, lid)){
						pthread_cond_wait(&retryLock.c, &retryLock.m);
					}
					std::cout<<"LockClient: client: "<<id<<" is allowed to retry lock:"<<lid<<std::endl;
					retryList.remove(lid);
					pthread_mutex_unlock(&retryLock.m);
					pthread_mutex_lock(&clientLock.m);
					continue;
				}
			}					
	}
	else{
		//pthread_mutex_lock(&lockStatusLock);
		while (it->second != Free){
			pthread_cond_wait(&clientLock.c, &clientLock.m);
			it = lockStatusMap.find(lid); // Check the lock status again
			if (it->second == None) //Error when adding Releasing here, why? one thread may always wait for retry rpc
				goto acquire;
		}
		it->second = Locked;
		//pthread_mutex_unlock(&lockStatusLock);
		std::cout<<"Client: "<<id<<" thread: "<<pthread_self()<<" set lock: "<<lid<<" to locked"<<std::endl;
		goto RELEASE_LOCK;
	}
		

	
	RELEASE_LOCK:
		pthread_mutex_unlock(&clientLock.m);
		return ret;
}

lock_protocol::status
lock_client_cache::release(lock_protocol::lockid_t lid)
{
	lock_protocol::status ret = lock_protocol::OK;
	int isInReleaseList;
	//When releasing we first check if this lock is in releaseList, if so we revoke the lock.
	//If not, we set the lock to free and wakeup sleeping thread
	
	pthread_mutex_lock(&clientLock.m);
		
	//check the lock status, it should be locked or free
	pthread_mutex_lock(&lockStatusLock);
	std::map<lock_protocol::lockid_t, int>::iterator it = lockStatusMap.find(lid);
	if (it->second != Locked && it->second != Free){
		std::cout<<"lockClient: Release lock: "<<lid<<" error, not locked or free!"<<std::endl;
		pthread_mutex_unlock(&lockStatusLock);
		ret = lock_protocol::IOERR;
		goto RELEASE_LOCK;
	}
	pthread_mutex_unlock(&lockStatusLock);

	pthread_mutex_lock(&releaseLock.m);
	pthread_mutex_lock(&lockStatusLock);
	//std::cout<<"Client: "<<id<<" thread: "<<pthread_self()<<" enter here"<<std::endl;
	isInReleaseList = isInList(releaseList, lid);

	if (isInReleaseList){ //in release list, wake up releaser
		it->second = Releasing;
		//std::cout<<"Client: "<<id<<" release lock: "<<lid<<" to releasing"<<std::endl;
		pthread_mutex_unlock(&lockStatusLock);
		pthread_cond_signal(&releaseLock.c);
		pthread_mutex_unlock(&releaseLock.m);		
			
		std::cout<<"Client: "<<id<<" release lock: "<<lid<<" to releasing2"<<std::endl;
		goto RELEASE_LOCK;	
	}
	else{ //not in release list, wake up other threads
		it->second = Free;
		//std::cout<<"Client: "<<id<<" release lock: "<<lid<<" to free"<<std::endl;	
		pthread_mutex_unlock(&lockStatusLock);
		pthread_mutex_unlock(&releaseLock.m);
		
		pthread_mutex_unlock(&clientLock.m);	
		pthread_cond_broadcast(&clientLock.c);
		std::cout<<"Client: "<<id<<" release lock: "<<lid<<" to free2"<<std::endl;
		return ret;
	}
	
	RELEASE_LOCK:
		pthread_mutex_unlock(&clientLock.m);
		return ret;
}

rlock_protocol::status 
lock_client_cache::revoke(lock_protocol::lockid_t lid, int &r)
{
	//std::cout<<"LockClient: client: "<<id<<" revoke!"<<std::endl;
	pthread_mutex_lock(&releaseLock.m);
	releaseList.push_back(lid);
	pthread_cond_broadcast(&releaseLock.c);
	pthread_mutex_unlock(&releaseLock.m);	
	std::cout<<"LockClient: client: "<<id<<" is asked to revoke lock:"<<lid<<std::endl;
	return rlock_protocol::OK;
}

rlock_protocol::status 
lock_client_cache::retry(lock_protocol::lockid_t lid, int &r)
{
	pthread_mutex_lock(&retryLock.m);
	retryList.push_back(lid);
	pthread_cond_broadcast(&retryLock.c);
	pthread_mutex_unlock(&retryLock.m);
	std::cout<<"LockClient: client: "<<id<<" is asked to retry lock:"<<lid<<std::endl;
	return rlock_protocol::OK;
}


