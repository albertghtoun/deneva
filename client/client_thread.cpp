/*
   Copyright 2015 Rachael Harding

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "global.h"
#include "client_thread.h"
#include "query.h"
#include "ycsb_query.h"
#include "tpcc_query.h"
#include "client_query.h"
#include "mem_alloc.h"
#include "transport.h"
#include "client_txn.h"
#include "msg_thread.h"
#include "msg_queue.h"
#include "wl.h"

void Client_thread_t::init(uint64_t thd_id, uint64_t node_id, Workload * workload) {
	_thd_id = thd_id;
	_node_id = node_id;
	_wl = workload;
}

uint64_t Client_thread_t::get_thd_id() { return _thd_id; }
uint64_t Client_thread_t::get_node_id() { return _node_id; }
uint64_t Client_thread_t::get_host_cid() {	return _host_cid; }
void Client_thread_t::set_host_cid(uint64_t cid) { _host_cid = cid; }
uint64_t Client_thread_t::get_cur_cid() { return _cur_cid; }
void Client_thread_t::set_cur_cid(uint64_t cid) {_cur_cid = cid; }

RC Client_thread_t::run_remote() {
	printf("Run_remote %ld:%ld\n",_node_id, _thd_id);

	BaseQuery * m_query = NULL;

	stats.init(get_thd_id());
	pthread_barrier_wait( &warmup_bar );
	// Send start msg to all nodes; wait for rsp from all nodes before continuing.
	int32_t inf;
  uint32_t return_node_offset;

	run_starttime = get_sys_clock();
  while(!_wl->sim_init_done) {
    tport_man.recv_msg();
    //while((m_query = work_queue.get_next_query(get_thd_id())) != NULL) {
    while(work_queue.dequeue(0,m_query)) { 
      assert(m_query->rtype == INIT_DONE);
      ATOM_SUB(_wl->rsp_cnt,1);
      printf("Received INIT_DONE from node %ld -- %ld\n",m_query->return_id,_wl->rsp_cnt);
      fflush(stdout);
      if(_wl->rsp_cnt ==0) {
        if( !ATOM_CAS(_wl->sim_init_done, false, true) )
          assert( _wl->sim_init_done);
      }
    }
    /*
    if(get_sys_clock() - run_starttime >= g_done_timer)
      return FINISH;
      */
  }
  warmup_done = true;
	int rsp_cnts[g_servers_per_client];
	memset(rsp_cnts, 0, g_servers_per_client * sizeof(int));
	pthread_barrier_wait( &warmup_bar );
	printf("Run_remote %ld:%ld\n",_node_id, _thd_id);

	myrand rdm;
	rdm.init(get_thd_id());
	ts_t rq_time = get_sys_clock();
	run_starttime = get_sys_clock();

	while (true) {
    if(get_sys_clock() - run_starttime >= g_done_timer) {
      break;
    }
		tport_man.recv_msg();
    //while((m_query = work_queue.get_next_query(get_thd_id())) != NULL) {
    while(work_queue.dequeue(0,m_query)) { 
			rq_time = get_sys_clock();
			assert(m_query->rtype == CL_RSP || m_query->rtype == EXP_DONE);
			assert(m_query->dest_id == g_node_id);
			switch (m_query->rtype) {
				case CL_RSP:
          return_node_offset = m_query->return_id - g_server_start_node;
          assert(return_node_offset < g_servers_per_client);
		      rsp_cnts[return_node_offset]++;
					inf = client_man.dec_inflight(return_node_offset);
          assert(inf >=0);
					break;
        case EXP_DONE:
          ATOM_SUB(_wl->done_cnt,1);
          break;
				default:
					assert(false);
			}
      qry_pool.put(m_query);
    }
		ts_t tend = get_sys_clock(); 
		if (warmup_finish && 
        ((_wl->sim_done && ((tend - rq_time) > MSG_TIMEOUT))
        || (get_sys_clock() - run_starttime >= g_done_timer))) {
      break;
		}

    // If all other nodes are done, finish.
		if (warmup_finish && _wl->done_cnt == 0) {
			if( !ATOM_CAS(_wl->sim_done, false, true) )
				assert( _wl->sim_done);
			if( !ATOM_CAS(_wl->sim_timeout, false, true) )
				assert( _wl->sim_timeout);
      printf("starting FINISH %ld:%ld\n",_node_id,_thd_id);
      fflush(stdout);
      printf("FINISH %ld:%ld\n",_node_id,_thd_id);
      fflush(stdout);
      return FINISH;
		}

    // This may be causing the client nodes to not finish
    /*
		if (_wl->sim_done && _wl->sim_timeout) {
			bool done = true;
			//for (uint32_t i = 0; i < g_node_cnt; ++i) {
			for (uint32_t i = 0; i < g_servers_per_client; ++i) {
				// Check if we're still waiting on any txns to finish
				inf = client_man.get_inflight(i);
				if (inf > 0 && _wl->done_cnt > 0) {
					done = false;
					break;
				}
			}
    }


			if (!done)
				continue;
        */
    if (_wl->sim_done && _wl->sim_timeout) {
      break;
    }
	}

  if( !ATOM_CAS(_wl->sim_timeout, false, true) ) {
    assert( _wl->sim_timeout);
  } else {
    printf("_wl->sim_timeout=%d\n",_wl->sim_timeout);
    fflush(stdout);
  }

      printf("starting FINISH %ld:%ld\n",_node_id,_thd_id);
      fflush(stdout);
  printf("FINISH %ld:%ld\n",_node_id,_thd_id);
  fflush(stdout);
  return FINISH;
}

RC Client_thread_t::run_send() {
	printf("Run_send %ld:%ld\n",_node_id, _thd_id);
  fflush(stdout);
	stats.init(get_thd_id());
	pthread_barrier_wait( &warmup_bar );

  MessageThread messager;
  messager.init(_thd_id);
	run_starttime = get_sys_clock();
	while (!_wl->sim_init_done) {
    messager.run();
    /*
    if(get_sys_clock() - run_starttime >= g_done_timer)
      return FINISH;
      */
  }

	pthread_barrier_wait( &warmup_bar );
	printf("Run_send %ld:%ld\n",_node_id, _thd_id);
  fflush(stdout);

	while (!(_wl->sim_done && _wl->sim_timeout)) {
    messager.run();
  }
      printf("starting FINISH %ld:%ld\n",_node_id,_thd_id);
      fflush(stdout);
  printf("FINISH %ld:%ld\n",_node_id,_thd_id);
  fflush(stdout);
	return FINISH;
}

RC Client_thread_t::run() {
	printf("Run %ld:%ld\n",_node_id, _thd_id);
	stats.init(get_thd_id());
	pthread_barrier_wait( &warmup_bar );
	BaseClientQuery * m_query = NULL;

	run_starttime = get_sys_clock();
	if( _thd_id == 0) {
#if WORKLOAD == YCSB
    m_query = new YCSBClientQuery;
#elif WORKLOAD == TPCC
    m_query = new TPCCClientQuery;
#endif
		uint64_t nnodes = g_node_cnt + g_client_node_cnt;
    /*
#if CC_ALG == CALVIN
		nnodes++;
#endif
*/
		for(uint64_t i = 0; i < nnodes; i++) {
			if(i != g_node_id) {
        msg_queue.enqueue(NULL,INIT_DONE,i);
			}
		}
  }
  /*
    if(get_sys_clock() - run_starttime >= g_done_timer)
      return FINISH;
	} else {
    while(!_wl->sim_init_done) {
      if(get_sys_clock() - run_starttime >= g_done_timer)
        return FINISH;
    }
  }
  */
	pthread_barrier_wait( &warmup_bar );
	printf("Run %ld:%ld\n",_node_id, _thd_id);

	myrand rdm;
	rdm.init(get_thd_id());

	uint64_t iters = 0;
	uint32_t num_txns_sent = 0;
	int txns_sent[g_servers_per_client];
  for (uint32_t i = 0; i < g_servers_per_client; ++i)
      txns_sent[i] = 0;

	run_starttime = get_sys_clock();
	uint64_t prog_time = run_starttime;

	//while (num_txns_sent < g_servers_per_client * MAX_TXN_PER_PART) {
  while(true) {
		//uint32_t next_node = iters++ % g_node_cnt;
		if(get_sys_clock() - run_starttime >= g_done_timer) {
      break;
    }
		uint32_t next_node = (((iters++) * g_client_thread_cnt) + _thd_id )% g_servers_per_client;
		// Just in case...
		if (iters == UINT64_MAX)
			iters = 0;
		if (client_man.inc_inflight(next_node) < 0)
			continue;

		m_query = client_query_queue.get_next_query(next_node,_thd_id);
		if (m_query == NULL) {
			client_man.dec_inflight(next_node);
      if(client_query_queue.done())
        break;
			continue;
		}
		DEBUG("Client: thread %lu sending query to node: %lu\n",
				_thd_id, GET_NODE_ID(m_query->pid));
    /*
		for (uint32_t k = 0; k < g_servers_per_client; ++k) {
			DEBUG("Node %u: txns in flight: %d\n", 
                    k + g_server_start_node, client_man.get_inflight(k));
    }
    */

    msg_queue.enqueue((BaseQuery*)((void*)m_query),RTXN,GET_NODE_ID(m_query->pid));
		num_txns_sent++;
		txns_sent[GET_NODE_ID(m_query->pid)-g_server_start_node]++;
    INC_STATS(get_thd_id(),txn_sent,1);

		if(get_sys_clock() - prog_time >= g_prog_timer) {
			prog_time = get_sys_clock();
			SET_STATS(get_thd_id(), tot_run_time, prog_time - run_starttime); 
      if(get_thd_id() == 0)
        stats.print_client(true);
    }
		if(get_sys_clock() - run_starttime >= g_done_timer) {
      break;
    }
	}


	for (uint64_t l = 0; l < g_servers_per_client; ++l)
		printf("Txns sent to node %lu: %d\n", l+g_server_start_node, txns_sent[l]);

  prog_time = get_sys_clock();
  SET_STATS(get_thd_id(), tot_run_time, prog_time - run_starttime); 

  if( _thd_id == 0) {
    // Send EXP_DONE to all nodes
    /*
    uint64_t nnodes = g_node_cnt + g_client_node_cnt;
#if WORKLOAD == YCSB
    m_query = new YCSBClientQuery;
#endif
    for(uint64_t i = 0; i < nnodes; i++) {
      if(i != g_node_id) {
        msg_queue.enqueue(NULL,EXP_DONE,i);
      }
		}
    */
    if( !ATOM_CAS(_wl->sim_done, false, true) ) {
      assert( _wl->sim_done);
    } else {
      printf("_wl->sim_done=%d\n",_wl->sim_done);
      fflush(stdout);
    }
  }
      printf("starting FINISH %ld:%ld\n",_node_id,_thd_id);
      fflush(stdout);
  printf("FINISH %ld:%ld\n",_node_id,_thd_id);
  fflush(stdout);
	return FINISH;
}
