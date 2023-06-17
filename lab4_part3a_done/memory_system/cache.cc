// ECE 430.322: Computer Organization
// Lab 4: Memory System Simulation

#include "cache.h"
#include <cstring>
#include <list>
#include <cassert>
#include <iostream>
#include <cmath>

using namespace std;

cache_c::cache_c(std::string name, int level, int num_set, int assoc, int line_size, int latency)
    : cache_base_c(name, num_set, assoc, line_size) {

  // instantiate queues
  m_in_queue   = new queue_c();
  m_out_queue  = new queue_c();
  m_fill_queue = new queue_c();
  m_wb_queue   = new queue_c();

  m_in_flight_wb_queue = new queue_c();

  m_id = 0;

  m_prev_i = nullptr;
  m_prev_d = nullptr;
  m_next = nullptr;
  m_memory = nullptr;

  m_latency = latency;
  m_level = level;

  // clock cycle
  m_cycle = 0;
  
  m_num_backinvals = 0;
  m_num_writebacks_backinval = 0;

  
}


cache_c::~cache_c() {
  delete m_in_queue;
  delete m_out_queue;
  delete m_fill_queue;
  delete m_wb_queue;
  delete m_in_flight_wb_queue;
}

/** 
 * Run a cycle for cache (DO NOT CHANGE)
 */
void cache_c::run_a_cycle() {
  // process the queues in the following order 
  // wb -> fill -> out -> in

  process_wb_queue();

  process_fill_queue();

  process_out_queue(); 

  process_in_queue();

  ++m_cycle;
}

void cache_c::back_invalidate(addr_t address) {
  int set_idx = (address / this->m_line_size) % this->m_num_sets;
  int tag = address / this->m_line_size / this->m_num_sets;

    for (int i = 0; i < m_set[set_idx]->m_assoc; i++) {
      if (m_set[set_idx]->m_entry[i].m_valid && m_set[set_idx]->m_entry[i].m_tag == tag) {
        m_set[set_idx]->m_entry[i].m_valid = false;
        //std::cout << "Back invalidated!!" << std::endl;
        m_num_backinvals++;
        //do writeback due to invalidating dirty, straight to memory
        if (m_set[set_idx]->m_entry[i].m_dirty) {
           //std::cout << "writeback occured from Back invalidated!!" << std::endl;
          m_set[set_idx]->m_entry[i].m_dirty = false;
          m_num_writebacks_backinval++;
        }
        break;
      }
    } 
}

bool cache_c::evict_and_bring_new(int set_idx, int tag, int req_type, bool set_dirty) {
  bool evicted, dirty_evicted; int evicted_tag;
  m_set[set_idx]->evict_and_bring_new(tag, set_dirty, evicted, dirty_evicted, evicted_tag);

  //assemble evicted address and back invalidate
  if (evicted && m_level != L1) {
    addr_t evicted_addr = m_line_size * set_idx + evicted_tag * m_line_size * m_num_sets;

    if (req_type == REQ_DFETCH || req_type == REQ_DSTORE)
      m_prev_d->back_invalidate(evicted_addr);
    else if (req_type == REQ_IFETCH)
      m_prev_i->back_invalidate(evicted_addr);
  }
  return dirty_evicted;
}

void cache_c::configure_neighbors(cache_c* prev_i, cache_c* prev_d, cache_c* next, simple_mem_c* memory) {
  m_prev_i = prev_i;
  m_prev_d = prev_d;
  m_next = next;
  m_memory = memory;
}

/**
 *
 * [Cache Fill Flow]
 *
 * This function puts the memory request into fill_queue, so that the cache
 * line is to be filled or written-back.  When we fill or write-back the cache
 * line, it will take effect after the intrinsic cache latency.  Thus, this
 * function adjusts the ready cycle of the request; i.e., a new ready cycle
 * needs to be set for the request.
 *
 */
bool cache_c::fill(mem_req_s* req) {
  //m_cycle += m_latency;
  req->m_rdy_cycle += m_latency;
  m_fill_queue->push(req);
}

/**
 * [Cache Access Flow]
 *
 * This function puts the memory request into in_queue.  When accessing the
 * cache, the outcome (e.g., hit/miss) will be known after the intrinsic cache
 * latency.  Thus, this function adjusts the ready cycle of the request; i.e.,
 * a new ready cycle needs to be set for the request .
 */
bool cache_c::access(mem_req_s* req) {
  //m_cycle += m_latency;
  req->m_rdy_cycle += m_latency;
  m_in_queue->push(req);
}

/** 
 * This function processes the input queue.
 * What this function does are
 * 1. iterates the requests in the queue
 * 2. performs a cache lookup in the "cache base" after the intrinsic access time
 * 3. on a cache hit, forward the request to the prev's fill_queue or the processor depending on the cache level.
 * 4. on a cache miss, put the current requests into out_queue
 */
void cache_c::process_in_queue() {
  while (!m_in_queue->empty()) {
    mem_req_s * req = m_in_queue->m_entry[0];
    m_in_queue->pop(req);

    need_writeback = false;


    bool hit = cache_base_c::access(req->m_addr, req->m_type, false);
    if (need_writeback) {
      req->m_dirty=true;
      m_wb_queue->push(req);
    }
    if (hit) {
      if (m_level == L1) {
        done_func(req);
      } else if (m_level == L2) {
        if (req->m_type == REQ_DFETCH || req->m_type == REQ_DSTORE) {
          //data read/write
          m_prev_d->fill(req);
        } else {
          //instruction fetch
          m_prev_i->fill(req);
        }
      }
    } else {
      m_out_queue->push(req);
    }
  }
} 

/** 
 * This function processes the output queue.
 * The function pops the requests from out_queue and accesses the next-level's cache or main memory.
 * CURRENT: There is no limit on the number of requests we can process in a cycle.
 */
void cache_c::process_out_queue() {
    while (!m_out_queue->empty()) {
    mem_req_s * req = m_out_queue->m_entry[0];
    m_out_queue->pop(req);
    if (m_next == nullptr) {
        m_memory->access(req);
        //i dont think dram calls fill of upper cache! ill do it
        fill(req);
    } else {
      if (!req->m_dirty) {
        m_next->access(req);
      }
      else
        m_next->fill(req);
    }
  }
}

/** 
 * This function processes the fill queue.  The fill queue contains both the
 * data from the lower level (and the dirty victim from the upper level. ????) 
 */

void cache_c::process_fill_queue() {
  while (!m_fill_queue->empty()) {
    mem_req_s * req = m_fill_queue->m_entry[0];
    m_fill_queue->pop(req);
    FILL_TYPE fill_type = req->m_dirty ? FILL_EVICT : FILL_INCLUDE;
    cache_base_c::access(req->m_addr, fill_type, true);

    if (fill_type == FILL_INCLUDE) {
      if (m_level == L1) {
        done_func(req);
      }
      else if (m_level == L2) {
        //since there is a cache above me, need to propatage the include fill upwards
        if (req->m_type == REQ_DFETCH || req->m_type == REQ_DSTORE) {
          //data read/write
          m_prev_d->fill(req);
        } else {
          //instruction fetch
          m_prev_i->fill(req);
        }
      }
    }
  }
}

/** 
 * This function processes the write-back queue.
 * The function basically moves the requests from wb_queue to out_queue.
 * CURRENT: There is no limit on the number of requests we can process in a cycle.
 */
void cache_c::process_wb_queue() {
  while (!m_wb_queue->empty()) {
    cout << "process wb queue" << endl;
    mem_req_s * req = m_wb_queue->m_entry[0];
    m_wb_queue->pop(req);

    m_out_queue->push(req);
  }
}

/**
 * Print statistics (DO NOT CHANGE)
 */
void cache_c::print_stats() {
  cache_base_c::print_stats();
  std::cout << "number of back invalidations: " << m_num_backinvals << "\n";
  std::cout << "number of writebacks due to back invalidations: " << m_num_writebacks_backinval << "\n";
}
