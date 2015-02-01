// the caching lock server implementation

#include "lock_server_cache.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

static void *
revokethread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->revoker();
  return 0;
}

static void *
retrythread(void *x)
{
  lock_server_cache *sc = (lock_server_cache *) x;
  sc->retryer();
  return 0;
}

lock_server_cache::lock_server_cache(class rsm *_rsm) 
  : rsm (_rsm)
{
  //Initialize the lock
  assert(pthread_mutex_init(&stateLock,0)==0);
  assert(pthread_mutex_init(&holderLock,0)==0);
  assert(pthread_mutex_init(&waitListLock,0)==0);
  assert(pthread_mutex_init(&rpccLock,0)==0);
  assert(pthread_mutex_init(&seqnumLock,0)==0);
  
  pthread_t th;
  int r = pthread_create(&th, NULL, &revokethread, (void *) this);
  assert (r == 0);
  r = pthread_create(&th, NULL, &retrythread, (void *) this);
  assert (r == 0);
}

lock_server_cache::~lock_server_cache()
{
	std::map<lock_protocol::lockid_t,lock_t>::iterator it;
	for(it = locks.begin(); it != locks.end(); it++){
		pthread_mutex_destroy(&((it->second).m));
		pthread_cond_destroy(&((it->second).c));
	}	
	locks.clear();
}

lock_protocol::status lock_server_cache::stat(std::string port, lock_protocol::lockid_t, int &r)
{
	std::cout<<"stat request from port:"<<port<<std::endl;
	r = 0;
	return lock_protocol::OK;
}

lock_protocol::status lock_server_cache::acquire(std::string clientId, int seqNum, lock_protocol::lockid_t lid, int &r)
{
	// If a acquire request comes in, either a new lock, or this lock is holded by some client, or it is unlocked
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&stateLock);	 //We might not need this lock
	// If this is a new lock, we create a new lock and give it to client
	if (locks.count(lid) == 0){
		locks[lid] = lock_t();
	}

	//Store seqNum per client per lock
	pthread_mutex_lock(&seqnumLock);
	std::pair<lock_protocol::lockid_t,std::string> lockClient = std::make_pair(lid, clientId);
	seqNumMap[lockClient] = seqNum; //If existed, update it. If not existed, push back it.		
	pthread_mutex_unlock(&seqnumLock);

	// Check the lock's status
	if (locks[lid].isLocked == false){
		pthread_mutex_lock(&locks[lid].m);
		locks[lid].isLocked = true; //change the status
		//Write down the new holder's name
		pthread_mutex_lock(&holderLock);
		lockToClient[lid] = clientId;
		pthread_mutex_unlock(&holderLock); 

		pthread_mutex_unlock(&locks[lid].m);
		cout<<"LockServer: Grant lockid: "<<lid<<" to clientId: "<<clientId<<std::endl;
		ret = lock_protocol::OK;
		goto release;
	}	
	else{   
		//If the lock is held by someone else, find the holder and revoke the lock from holder
		std::map<lock_protocol::lockid_t,std::string>::iterator it = lockToClient.find(lid);
		if (it == lockToClient.end()){
			cout<<"LockServer: Error: the lock is locked, but no one has that lock!"<<std::endl;	
			ret = lock_protocol::IOERR;
			goto release;	
		}

		pthread_mutex_lock(&revokeLock.m);		
		//if (revokeList.empty()){ //If it is empty, we need to wakeup the revoker thread (WRONG, thread mighe never wake up)
		//	revokeList.push_back(lid);
		//	pthread_cond_signal(&revokeLock.c);	
		//}
		//else
		//	revokeList.push_back(lid);
		revokeList.push_back(lid);
		//std::cout<<"revokeList: ";
	        //for (std::list<lock_protocol::lockid_t>::iterator it=revokeList.begin(); it != revokeList.end(); ++it)
	        //	std::cout << ' ' << *it;
		//std::cout<<std::endl;
		pthread_cond_signal(&revokeLock.c);
		pthread_mutex_unlock(&revokeLock.m);

		//Add it to waitList
		pthread_mutex_lock(&waitListLock);
		retryMsg msg;
		msg.lid = lid;
		msg.clientId = clientId;
		msg.seqNum = seqNum;
		waitList.push_back(msg);
		pthread_mutex_unlock(&waitListLock);
		
		//All done, tell client RETRY after receiving the retry request	
		cout<<"LockServer: RETRY lockid: "<<lid<<" to clientId: "<<clientId<<std::endl;
		ret = lock_protocol::RETRY;	
		goto release;	
	}

	release:
		pthread_mutex_unlock(&stateLock);
		return ret;
}

lock_protocol::status lock_server_cache::release(std::string clientId, int seqNum, lock_protocol::lockid_t lid, int &r)
{
	//cout<<"LockServer: Client "<<clientId<<" try to release lock: "<<lid<<std::endl;
	// When a release request comes in, first we unlocked the lock, then tell clients to retry acquire request
	lock_protocol::status ret = lock_protocol::OK;
	pthread_mutex_lock(&stateLock);	 //We might not need this lock

	//We first ensure the lock is existed in our locks
	if (locks.count(lid) == 0){
		std::cout<<"LockServer: Error: Requested release lock is not in locklist"<<std::endl;
		ret = lock_protocol::IOERR;
		goto release;
	}

	//Then we can release the lock
	pthread_mutex_lock(&locks[lid].m);
	locks[lid].isLocked = false;

	//Erase the corresponding holder from lockToClient
	pthread_mutex_lock(&holderLock);
	lockToClient.erase(lid);
	pthread_mutex_unlock(&holderLock);
	
	pthread_mutex_unlock(&locks[lid].m);

	//Add the lock to retryList, wakeup the retryer thread
	pthread_mutex_lock(&retryLock.m);		
	retryList.push_back(lid);
	pthread_cond_signal(&retryLock.c);	
	pthread_mutex_unlock(&retryLock.m);
	
	cout<<"LockServer: Client "<<clientId<<" release lock: "<<lid<<std::endl;
	
	//everything is done, go to release
	goto release;	

	release:
		pthread_mutex_unlock(&stateLock);
		return ret;
}

void
lock_server_cache::revoker()
{

  // This method should be a continuous loop, that sends revoke
  // messages to lock holders whenever another client wants the
  // same lock
	while(1){
		lock_protocol::lockid_t lid;
		pthread_mutex_lock(&revokeLock.m);
		while(revokeList.empty()) //When list is empty, sleep the thread
			pthread_cond_wait(&revokeLock.c, &revokeLock.m);
		//Pop out the first element
		lid = revokeList.front();
		revokeList.pop_front();
		pthread_mutex_unlock(&revokeLock.m);

		//Find the client that holds this lock
		pthread_mutex_lock(&holderLock);
		std::string clid;
		std::map<lock_protocol::lockid_t,std::string>::iterator it = lockToClient.find(lid); // get client Id
		if (it == lockToClient.end()){
			std::cout<<"LockServer : Revoker warning : revoke lock is not held by anyone."<<std::endl;
			pthread_mutex_unlock(&holderLock);
			continue;
		}
		else
			clid = it->second;
		pthread_mutex_unlock(&holderLock);

		//Tell client to revoke the lock via rpc
		pthread_mutex_lock(&rpccLock);
		int r;
		rlock_protocol::status ret;
		std::map<std::string, rpcc*>::iterator rpcIt = clientToRpcc.find(clid); // find rpcc in our rpcc map
		if (rpcIt == clientToRpcc.end()){
			sockaddr_in dstsock;
			make_sockaddr(clid.c_str(), &dstsock);
			rpcc *newRpcc = new rpcc(dstsock); //FIXME: how to free it?
		  	if (newRpcc->bind() < 0) {
		    		printf("LockServer: revoker call bind failure\n");
		  	}
			clientToRpcc[clid] = newRpcc; //Insert this rpcc into our map
			ret = newRpcc->call(rlock_protocol::revoke, lid, r); //FIXME: what if ret is RPCERR?
			if (ret != rlock_protocol::OK)
				std::cout<<"LockServer: revoker call might not succeed!"<<std::endl;
			else
				std::cout<<"LockServer: revoker rpc to client: "<<clid<<" for lock: "<<lid<<std::endl;
		}
		else{
			ret = rpcIt->second->call(rlock_protocol::revoke, lid, r);
			if (ret != rlock_protocol::OK)
				std::cout<<"LockServer: revoker call might not succeed!"<<std::endl;
			else
				std::cout<<"LockServer: revoker rpc to client: "<<clid<<" for lock: "<<lid<<std::endl;		
		}
		pthread_mutex_unlock(&rpccLock);
	}

}


void
lock_server_cache::retryer()
{

  // This method should be a continuous loop, waiting for locks
  // to be released and then sending retry messages to those who
  // are waiting for it.
	while(1){
		lock_protocol::lockid_t lid;
		//First we find which lock is released
		pthread_mutex_lock(&retryLock.m);
		while(retryList.empty()) //When list is empty, sleep the thread
			pthread_cond_wait(&retryLock.c, &retryLock.m);
		//Pop out the first element
		lid = retryList.front();
		retryList.pop_front();
		pthread_mutex_unlock(&retryLock.m);	
		
		//now we get out all client due to the released lock id, which needs to be sent retry RPC
		pthread_mutex_lock(&waitListLock);
		//std::cout<<std::endl;
		//std::cout<<"waitList: "<<std::endl;
	        //for (std::vector<retryMsg>::iterator wit=waitList.begin(); wit != waitList.end(); ++wit){
	        //	std::cout <<"client: " <<wit->clientId<<" lock: "<<wit->lid<<" seqNum: "<<wit->seqNum<<std::endl;
		//}
		//std::cout<<std::endl;
		std::vector<std::string> retryRPCList;
		for (std::vector<retryMsg>::iterator i=waitList.begin(); i!=waitList.end(); ){
			retryMsg tempMsg = *i;
			if (tempMsg.lid == lid){
				i = waitList.erase(i); //delete this wait
				pthread_mutex_lock(&seqnumLock);
				int curSeqNum = seqNumMap.find(std::make_pair(lid, tempMsg.clientId))->second;
				pthread_mutex_unlock(&seqnumLock);	
				if (tempMsg.seqNum == curSeqNum)
					retryRPCList.push_back(tempMsg.clientId);						
			}
			else
				i++;
		}
		//std::cout<<"retryRPCList: ";
	        //for (std::vector<std::string>::iterator it=retryRPCList.begin(); it != retryRPCList.end(); ++it)
	        //	std::cout << ' ' << *it;
		//std::cout<<std::endl;
		pthread_mutex_unlock(&waitListLock);	

		//Send retry RPC to all clients in my retryRPCList
		for (unsigned int i=0; i<retryRPCList.size(); i++){
			std::string clid = retryRPCList.at(i);

			//Tell client to revoke the lock via rpc
			pthread_mutex_lock(&rpccLock);
			int r;
			rlock_protocol::status ret;
			std::map<std::string, rpcc*>::iterator rpcIt = clientToRpcc.find(clid); // find rpcc in our rpcc map
			if (rpcIt == clientToRpcc.end()){
				sockaddr_in dstsock;
				make_sockaddr(clid.c_str(), &dstsock);
				rpcc *newRpcc = new rpcc(dstsock); //FIXME: how to free it?
			  	if (newRpcc->bind() < 0) {
			    		printf("LockServer: retryer call bind failure\n");
			  	}
				clientToRpcc[clid] = newRpcc; //Insert this rpcc into our map
				ret = newRpcc->call(rlock_protocol::retry, lid, r); //FIXME: what if ret is RPCERR?
				if (ret != rlock_protocol::OK)
					std::cout<<"LockServer: retryer call might not succeed!"<<std::endl;
				else
					std::cout<<"LockServer: retry rpc to client: "<<clid<<" for lock: "<<lid<<std::endl;
			}
			else{
				ret = rpcIt->second->call(rlock_protocol::retry, lid, r);
				if (ret != rlock_protocol::OK)
					std::cout<<"LockServer: retryer call might not succeed!"<<std::endl;	
				else
					std::cout<<"LockServer: retry rpc to client: "<<clid<<" for lock: "<<lid<<std::endl;	
			}
			pthread_mutex_unlock(&rpccLock);
		}
	}
}





