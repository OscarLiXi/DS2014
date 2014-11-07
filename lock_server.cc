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
	//int x = lid & 0xff;	
	printf("server:acquire %016llx request from clt %d\n", lid, clt);
	
	if(!lock_mutex.count(lid)){ //check if mutex already corresponds tto lockid
		pthread_mutex_t mutex;
		pthread_mutex_init(&mutex,NULL);
		lock_mutex[lid] = mutex;	
	}
	pthread_mutex_lock(&lock_mutex[lid]);	
	printf("server:grant lid %016llx to clt %d\n", lid, clt);
	return ret;
}

lock_protocol::status
lock_server::release(int clt, lock_protocol::lockid_t lid, int &r)
{
	lock_protocol::status ret = lock_protocol::OK;
	//int x = lid & 0xff;
	printf("server:release %016llx request from clt %d\n", lid, clt);
	pthread_mutex_unlock(&lock_mutex[lid]);
	printf("server:release lid %016llx of clt %d\n", lid, clt);
	return ret;
}

