// the lock server implementation

#include "lock_server.h"
#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>

lock_server::lock_server():
  nacquire (0)
{
}

lock_server::~lock_server()
{
	std::map<lock_protocol::lockid_t,lock_t>::iterator it;
	for(it = locks.begin(); it != locks.end(); it++){
		pthread_mutex_destroy(&((it->second).m));
		pthread_cond_destroy(&((it->second).c));
	}	
	locks.clear();
}

lock_protocol::status
lock_server::stat(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	printf("stat request from clt %d\n", clt);
	r = nacquire;
	return ret;
}

lock_protocol::status
lock_server::acquire(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	printf("server:acquire %016llx request from clt %d\n", lid, clt);
	//create a new lock if lock id not exist	
	if(!locks.count(lid))
		locks[lid] = lock_t();	
	pthread_mutex_lock(&locks[lid].m);	
	//if it is locked, then wait until it is signaled
	while(locks[lid].isLocked)
		pthread_cond_wait(&locks[lid].c,&locks[lid].m);
	//lock it again
	locks[lid].isLocked = true;
	pthread_mutex_unlock(&locks[lid].m);
	printf("server:grant lid %016llx to clt %d\n", lid, clt);
	return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	printf("server:release %016llx request from clt %d\n", lid, clt);
	pthread_mutex_lock(&locks[lid].m);
	//release the lock
	locks[lid].isLocked = false;
	//signal the waiting threads
	pthread_cond_signal(&locks[lid].c);
	pthread_mutex_unlock(&locks[lid].m);
	return ret;
}

