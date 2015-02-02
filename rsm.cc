//
// Replicated state machine implementation with a primary and several
// backups. The primary receives requests, assigns each a view stamp (a
// vid, and a sequence number) in the order of reception, and forwards
// them to all backups. A backup executes requests in the order of that
// the primary stamps them and replies with an OK to the primary. The
// primary executes the request after it receives OKs from all backups,
// and sends the reply back to the client.
//
// The config module will tell the RSM about a new view. If the
// primary in the previous view is a member of the new view, then it
// will stay the primary.  Otherwise, the smallest numbered node of
// the previous view will be the new primary.  In either case, the new
// primary will be a node from the previous view.  The configuration
// module constructs the sequence of views for the RSM and the RSM
// ensures there will be always one primary, who was a member of the
// last view.
//
// When a new node starts, the recovery thread is in charge of joining
// the RSM.  It will collect the internal RSM state from the primary;
// the primary asks the config module to add the new node and returns
// to the joining the internal RSM state (e.g., paxos log). Since
// there is only one primary, all joins happen in well-defined total
// order.
//
// The recovery thread also runs during a view change (e.g, when a node
// has failed).  After a failure some of the backups could have
// processed a request that the primary has not, but those results are
// not visible to clients (since the primary responds).  If the
// primary of the previous view is in the current view, then it will
// be the primary and its state is authoritive: the backups download
// from the primary the current state.  A primary waits until all
// backups have downloaded the state.  Once the RSM is in sync, the
// primary accepts requests again from clients.  If one of the backups
// is the new primary, then its state is authoritative.  In either
// scenario, the next view uses a node as primary that has the state
// resulting from processing all acknowledged client requests.
// Therefore, if the nodes sync up before processing the next request,
// the next view will have the correct state.
//
// While the RSM in a view change (i.e., a node has failed, a new view
// has been formed, but the sync hasn't completed), another failure
// could happen, which complicates a view change.  During syncing the
// primary or backups can timeout, and initiate another Paxos round.
// There are 2 variables that RSM uses to keep track in what state it
// is:
//    - inviewchange: a node has failed and the RSM is performing a view change
//    - insync: this node is syncing its state
//
// If inviewchange is false and a node is the primary, then it can
// process client requests. If it is true, clients are told to retry
// later again.  While inviewchange is true, the RSM may go through several
// member list changes, one by one.   After a member list
// change completes, the nodes tries to sync. If the sync complets,
// the view change completes (and inviewchange is set to false).  If
// the sync fails, the node may start another member list change
// (inviewchange = true and insync = false).
//
// The implementation should be used only with servers that run all
// requests run to completion; in particular, a request shouldn't
// block.  If a request blocks, the backup won't respond to the
// primary, and the primary won't execute the request.  A request may
// send an RPC to another host, but the RPC should be a one-way
// message to that host; the backup shouldn't do anything based on the
// response or execute after the response, because it is not
// guaranteed that all backup will receive the same response and
// execute in the same order.
//
// The implementation can be viewed as a layered system:
//       RSM module     ---- in charge of replication
//       config module  ---- in charge of view management
//       Paxos module   ---- in charge of running Paxos to agree on a value
//
// Each module has threads and internal locks. Furthermore, a thread
// may call down through the layers (e.g., to run Paxos's proposer).
// When Paxos's acceptor accepts a new value for an instance, a thread
// will invoke an upcall to inform higher layers of the new value.
// The rule is that a module releases its internal locks before it
// upcalls, but can keep its locks when calling down.

#include <fstream>
#include <iostream>

#include "handle.h"
#include "rsm.h"
#include "rsm_client.h"

static void *
recoverythread(void *x)
{
  rsm *r = (rsm *) x;
  r->recovery();
  return 0;
}



rsm::rsm(std::string _first, std::string _me) 
  : stf(0), primary(_first), insync (false), inviewchange (false), nbackup (0), partitioned (false), dopartition(false), break1(false), break2(false)
{
  pthread_t th;

  last_myvs.vid = 0;
  last_myvs.seqno = 0;
  myvs = last_myvs;
  myvs.seqno = 1;

  pthread_mutex_init(&rsm_mutex, NULL);
  pthread_mutex_init(&invoke_mutex, NULL);
  pthread_cond_init(&recovery_cond, NULL);
  pthread_cond_init(&sync_cond, NULL);
  pthread_cond_init(&join_cond, NULL);

  cfg = new config(_first, _me, this);

  rsmrpc = cfg->get_rpcs();
  rsmrpc->reg(rsm_client_protocol::invoke, this, &rsm::client_invoke);
  rsmrpc->reg(rsm_client_protocol::members, this, &rsm::client_members);
  rsmrpc->reg(rsm_protocol::invoke, this, &rsm::invoke);
  rsmrpc->reg(rsm_protocol::transferreq, this, &rsm::transferreq);
  rsmrpc->reg(rsm_protocol::transferdonereq, this, &rsm::transferdonereq);
  rsmrpc->reg(rsm_protocol::joinreq, this, &rsm::joinreq);

  // tester must be on different port, otherwise it may partition itself
  testsvr = new rpcs(atoi(_me.c_str()) + 1);
  testsvr->reg(rsm_test_protocol::net_repair, this, &rsm::test_net_repairreq);
  testsvr->reg(rsm_test_protocol::breakpoint, this, &rsm::breakpointreq);
  //printf("rsm::rsm change before lock!\n");
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  
  assert(pthread_create(&th, NULL, &recoverythread, (void *) this) == 0);

  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  //printf("rsm::rsm change after lock!\n");
}

void
rsm::reg1(int proc, handler *h)
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  procs[proc] = h;
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
}

// The recovery thread runs this function
void
rsm::recovery()
{
  bool r = false;
  inviewchange = true;
  assert(pthread_mutex_lock(&rsm_mutex)==0);

  while (1) {
    while (!cfg->ismember(cfg->myaddr())) {
      if (join(primary)) {
		printf("recovery: joined\n");
		if(primary == cfg->myaddr())
           r = sync_with_backups();
        else
           r = sync_with_primary();	
      } else {
	assert(pthread_mutex_unlock(&rsm_mutex)==0);
	printf("rsm::recovery go to sleep(30)\n");
	sleep (30); // XXX make another node in cfg primary?
	assert(pthread_mutex_lock(&rsm_mutex)==0);
      }
    }

    if (r) inviewchange = false;
    printf("recovery: go to sleep %d %d\n", insync, inviewchange);
    pthread_cond_wait(&recovery_cond, &rsm_mutex);
  }
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
}

bool
rsm::sync_with_backups()
{
  // For lab 8
  // For lab 8
        bool ret = true;
        assert(pthread_mutex_lock(&rsm_mutex) == 0);
        std::vector<std::string> cur_view = cfg->get_curview();
        for(int i = 0; i < cur_view.size(); i++){
                if(primary == cur_view[i])
                        continue;
                handle h(cur_view[i]);
                if(h.get_rpcc()){
                        int r = 0;
                        if(h.get_rpcc()->call(rsm_protocol::transferdonereq,
                                        cfg->myaddr(), r, rpcc::to(1000)) != rsm_protocol::OK ){

                                printf("rsm::sync_with_backups: RPC failed!\n");
                        }
                }
                else{
                        printf("rsm::sync_with_backups: get_rpcc() failed!\n");
                }
        }

        assert(pthread_mutex_unlock(&rsm_mutex) == 0);
        return ret;
}


bool
rsm::sync_with_primary()
{
  // For lab 8
assert(pthread_mutex_lock(&rsm_mutex) == 0);
        bool r = statetransfer(primary);
        assert(pthread_mutex_unlock(&rsm_mutex) == 0);
        return r;
}
/*rsm_protocol::status
rsm::transferreq(std::string src, viewstamp last, rsm_protocol::transferres &r){

        assert(pthread_mutex_lock(&rsm_mutex) == 0);
        r.state = stf->marshal_state();
        r.last = last_myvs;
        assert(pthread_mutex_unlock(&rsm_mutex) == 0);
        return rsm_protocol::OK;
}*/

bool
rsm::donereq(std::string m, int &r)
{
  int ret = rsm_client_protocol::OK;
  assert (pthread_mutex_lock(&rsm_mutex) == 0);
  // For lab 8
  assert (pthread_mutex_unlock(&rsm_mutex) == 0);
  return ret;
}

/*rsm_protocol::status
rsm::transferdonereq(std::string m, int &r){

        if(donereq(m,r))
                return rsm_protocol::OK;
        else
                return rsm_protocol::ERR;
}*/

/**
 * Call to transfer state from m to the local node.
 * Assumes that rsm_mutex is already held.
 */
bool
rsm::statetransfer(std::string m)
{
  rsm_protocol::transferres r;
  handle h(m);
  int ret;
  printf("rsm::statetransfer: contact %s w. my last_myvs(%d,%d)\n", 
	 m.c_str(), last_myvs.vid, last_myvs.seqno);
  if (h.get_rpcc()) {
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    ret = h.get_rpcc()->call(rsm_protocol::transferreq, cfg->myaddr(), 
			     last_myvs, r, rpcc::to(1000));
    assert(pthread_mutex_lock(&rsm_mutex)==0);
  }
  if (h.get_rpcc() == 0 || ret != rsm_protocol::OK) {
    printf("rsm::statetransfer: couldn't reach %s %lx %d\n", m.c_str(), 
	   (long unsigned) h.get_rpcc(), ret);
    return false;
  }
  if (stf && last_myvs != r.last) {
    stf->unmarshal_state(r.state);
  }
  last_myvs = r.last;
  printf("rsm::statetransfer transfer from %s success, vs(%d,%d)\n", 
	 m.c_str(), last_myvs.vid, last_myvs.seqno);
  return true;
}

bool
rsm::statetransferdone(std::string m) {
  // For lab 8
  return true;
}


bool
rsm::join(std::string m) {
  handle h(m);
  int ret ;
  rsm_protocol::joinres r;

  if (h.get_rpcc() != 0) {
    printf("rsm::join: %s mylast (%d,%d)\n", m.c_str(), last_myvs.vid, 
	   last_myvs.seqno);
    //printf("rsm::join before unlock!\n");
    assert(pthread_mutex_unlock(&rsm_mutex)==0);
    
    ret = h.get_rpcc()->call(rsm_protocol::joinreq, cfg->myaddr(), last_myvs, 
			     r, rpcc::to(120000));
    //printf("rsm::join after unlock!\n");
    assert(pthread_mutex_lock(&rsm_mutex)==0);
  }
  if (h.get_rpcc() == 0 || ret != rsm_protocol::OK) {
    printf("rsm::join: couldn't reach %s %p %d\n", m.c_str(), 
	   h.get_rpcc(), ret);
    return false;
  }
  printf("rsm::join: succeeded %s\n", r.log.c_str());
  cfg->restore(r.log);
  
  return true;
}


/*
 * Config informs rsm whenever it has successfully 
 * completed a view change
 */

void 
rsm::commit_change() 
{

  //printf("rsm::commit_change: require rsm_mutex lock!\n");
  //pthread_mutex_lock(&rsm_mutex);
  //printf("rsm::commit_change: get rsm_mutex lock!\n");
  // Lab 7:
  // - If I am not part of the new view, start recovery
  set_primary();
  pthread_cond_broadcast(&recovery_cond); 
}


std::string
rsm::execute(int procno, std::string req)
{
  printf("execute\n");
  handler *h = procs[procno];
  assert(h);
  unmarshall args(req);
  marshall rep;
  std::string reps;
  rsm_protocol::status ret = h->fn(args, rep);
  marshall rep1;
  rep1 << ret;
  rep1 << rep.str();

  //std::cout<<"rsm::excute return:"<<rep1.str()<<std::endl;
  return rep1.str();
}

//
// Clients call client_invoke to invoke a procedure on the replicated state
// machine: the primary receives the request, assigns it a sequence
// number, and invokes it on all members of the replicated state
// machine.
//
rsm_client_protocol::status
rsm::client_invoke(int procno, std::string req, std::string &r)
{
  	// For lab 8
  	pthread_mutex_lock(&invoke_mutex);
	int ret = rsm_client_protocol::OK;
	viewstamp vs;
	std::vector<std::string> cur_view;
	if(inviewchange){
		ret = rsm_client_protocol::BUSY;
		goto release;
	}
	if(!amiprimary()){
		ret = rsm_client_protocol::NOTPRIMARY;
		goto release;
	}
	vs = viewstamp(myvs.vid, myvs.seqno++);	
	cur_view = cfg->get_curview();
	//forward request to slaves, wait till receive OK from all the slaves
	for(int i = 0; i < cur_view.size(); i++){
		//only forward request to slaves
		if(cfg->myaddr() == cur_view[i])	
			continue;
		handle h(cur_view[i]);
		if(h.get_rpcc()){
			int r_dummy;
			if(h.get_rpcc()->call(rsm_protocol::invoke, procno, 
				vs, req, r_dummy, rpcc::to(1000)) != rsm_client_protocol::OK ){
				
				printf("rsm::client_invoke: RPC failed!\n");
				while(!cfg->remove(cur_view[i])) ;
				continue;
			}
		}
		else{
			printf("rsm::client_invoke: get_rpcc() failed!\n");
			while(!cfg->remove(cur_view[i])) ;
			continue;
		}
	}
	//execute request locally
	r = execute(procno, req);
	printf("rsm::client_invoke: after execute()\n");
release:
  	pthread_mutex_unlock(&invoke_mutex);
  	return ret;
}

// 
// The primary calls the internal invoke at each member of the
// replicated state machine 
//
// the replica must execute requests in order (with no gaps) 
// according to requests' seqno 

rsm_protocol::status
rsm::invoke(int proc, viewstamp vs, std::string req, int &dummy)
{
  	// For lab 8
  	// check if it is a slave in current view
  	if(!cfg->ismember(cfg->myaddr()) || amiprimary()){
  		printf("rsm::invoke: not a slave in current view\n");
		return rsm_client_protocol::ERR;
  	}
	if(vs.seqno != last_myvs.seqno + 1){
		printf("rsm::invoke: not an expected request\n");
		return rsm_client_protocol::ERR;
	}
	execute(proc,req);
	last_myvs = vs;
  	return rsm_client_protocol::OK;
}

/**
 * RPC handler: Send back the local node's state to the caller
 */
rsm_protocol::status
rsm::transferreq(std::string src, viewstamp last, rsm_protocol::transferres &r)
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  int ret = rsm_protocol::OK;
  printf("transferreq from %s (%d,%d) vs (%d,%d)\n", src.c_str(), 
	 last.vid, last.seqno, last_myvs.vid, last_myvs.seqno);
  if (stf && last != last_myvs) 
    r.state = stf->marshal_state();
  r.last = last_myvs;
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  return ret;
}

/**
  * RPC handler: Send back the local node's latest viewstamp
  */
rsm_protocol::status
rsm::transferdonereq(std::string m, int &r)
{
  int ret = rsm_client_protocol::OK;
  assert (pthread_mutex_lock(&rsm_mutex) == 0);
  // For lab 8
  assert (pthread_mutex_unlock(&rsm_mutex) == 0);
  return ret;
}

rsm_protocol::status
rsm::joinreq(std::string m, viewstamp last, rsm_protocol::joinres &r)
{
  int ret = rsm_client_protocol::OK;
  //printf("rsm::joinreq change before lock!\n");
  //std::cout<<"rsm::joinreq my thread id: "<<pthread_self()<<std::endl;  
  //printf("rsm::joinreq my thread id:%d\n", pthread_self());
  assert (pthread_mutex_lock(&rsm_mutex) == 0);
  printf("joinreq: src %s last (%d,%d) mylast (%d,%d)\n", m.c_str(), 
	 last.vid, last.seqno, last_myvs.vid, last_myvs.seqno);
  if (cfg->ismember(m)) {
    printf("joinreq: is still a member\n");

    r.log = cfg->dump();
  } else if (cfg->myaddr() != primary) {
    printf("joinreq: busy\n");
    ret = rsm_client_protocol::BUSY;
  } else {
    // Lab 7: invoke config to create a new view that contains m
		inviewchange = true;
	 	//pthread_mutex_unlock(&rsm_mutex);	
		if( cfg->add(m)){
			r.log = cfg->dump();
			//printf("rsm::joinreq: call config to add %s success\n",m.c_str());
		}
		else {
			printf("rsm::joinreq: call config to add %s fail\n",m.c_str());
			ret = rsm_client_protocol::ERR;
		}
  } 
  //pthread_mutex_lock(&rsm_mutex);
  assert (pthread_mutex_unlock(&rsm_mutex) == 0);
  //printf("rsm::joinreq change after lock!\n");
  return ret;
}

/*
 * RPC handler: Send back all the nodes this local knows about to client
 * so the client can switch to a different primary 
 * when it existing primary fails
 */
rsm_client_protocol::status
rsm::client_members(int i, std::vector<std::string> &r)
{
  std::vector<std::string> m;
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  m = cfg->get_curview();
  m.push_back(primary);
  r = m;
  printf("rsm::client_members return %s m %s\n", cfg->print_curview().c_str(),
	 primary.c_str());
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  return rsm_protocol::OK;
}

// if primary is member of new view, that node is primary
// otherwise, the lowest number node of the previous view.
// caller should hold rsm_mutex
void
rsm::set_primary()
{
  std::vector<std::string> c = cfg->get_curview();
  std::vector<std::string> p = cfg->get_prevview();
  assert (c.size() > 0);

  if (isamember(primary,c)) {
    printf("set_primary: primary stays %s\n", primary.c_str());
    return;
  }

  assert(p.size() > 0);
  for (unsigned i = 0; i < p.size(); i++) {
    if (isamember(p[i], c)) {
      primary = p[i];
      printf("set_primary: primary is %s\n", primary.c_str());
      return;
    }
  }
  assert(0);
}

// Assume caller holds rsm_mutex
bool
rsm::amiprimary_wo()
{
  return primary == cfg->myaddr() && !inviewchange;
}

bool
rsm::amiprimary()
{
  //printf("rsm::amiprimary change before lock!\n");
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  bool r = amiprimary_wo();
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  //printf("rsm::amiprimary change after lock!\n");
  return r;
}


// Testing server

// Simulate partitions

// assumes caller holds rsm_mutex
void
rsm::net_repair_wo(bool heal)
{
  std::vector<std::string> m;
  m = cfg->get_curview();
  for (unsigned i  = 0; i < m.size(); i++) {
    if (m[i] != cfg->myaddr()) {
        handle h(m[i]);
	printf("rsm::net_repair_wo: %s %d\n", m[i].c_str(), heal);
	if (h.get_rpcc()) h.get_rpcc()->set_reachable(heal);
    }
  }
  rsmrpc->set_reachable(heal);
}

rsm_test_protocol::status 
rsm::test_net_repairreq(int heal, int &r)
{
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  printf("rsm::test_net_repairreq: %d (dopartition %d, partitioned %d)\n", 
	 heal, dopartition, partitioned);
  if (heal) {
    net_repair_wo(heal);
    partitioned = false;
  } else {
    dopartition = true;
    partitioned = false;
  }
  r = rsm_test_protocol::OK;
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  return r;
}

// simulate failure at breakpoint 1 and 2

void 
rsm::breakpoint1()
{
  if (break1) {
    printf("Dying at breakpoint 1 in rsm!\n");
    exit(1);
  }
}

void 
rsm::breakpoint2()
{
  if (break2) {
    printf("Dying at breakpoint 2 in rsm!\n");
    exit(1);
  }
}

void 
rsm::partition1()
{
  if (dopartition) {
    net_repair_wo(false);
    dopartition = false;
    partitioned = true;
  }
}

rsm_test_protocol::status
rsm::breakpointreq(int b, int &r)
{
  r = rsm_test_protocol::OK;
  assert(pthread_mutex_lock(&rsm_mutex)==0);
  printf("rsm::breakpointreq: %d\n", b);
  if (b == 1) break1 = true;
  else if (b == 2) break2 = true;
  else if (b == 3 || b == 4) cfg->breakpoint(b);
  else r = rsm_test_protocol::ERR;
  assert(pthread_mutex_unlock(&rsm_mutex)==0);
  return r;
}




