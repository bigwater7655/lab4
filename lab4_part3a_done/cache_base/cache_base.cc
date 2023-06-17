// ECE 430.322: Computer Organization
// Lab 4: Memory System Simulation

/**
 *
 * This is the base cache structure that maintains and updates the tag store
 * depending on a cache hit or a cache miss. Note that the implementation here
 * will be used throughout Lab 4. 
 */

#include "cache_base.h"
#include "atom/mem_req.h"

#include <cmath>
#include <string>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iomanip>

/**
 * This allocates an "assoc" number of cache entries per a set
 * @param assoc - number of cache entries in a set
 */
cache_set_c::cache_set_c(int assoc) {
  m_entry = new cache_entry_c[assoc];
  m_assoc = assoc;

  //init access order to arbitrary order
  for (int i = 0; i < assoc; i++)
    access_order[i] = i;
}

// cache_set_c destructor
cache_set_c::~cache_set_c() {
  delete[] m_entry;
}

void cache_set_c::update_access_order(int entry_no) {
  int loc;
  for (loc = 0; loc < m_assoc; loc++) {
    if (access_order[loc] == entry_no) break;
  }
  for (int i = loc; i >= 1; i--) {
    access_order[i] = access_order[i-1];
  }
  access_order[0] = entry_no;
}

//true if dirty is evicted
void cache_set_c::evict_and_bring_new(int tag, bool set_dirty, bool& evicted, bool& dirty_evicted, int& evicted_tag) {
  //when writing to cache, first find if there are any invalid (vacant) blocks
  int i;
  evicted = true; //return whether it is evicted
  bool invalid_found = false;
    for (i = 0; i < m_assoc; i++) {
      if (!m_entry[i].m_valid) {
        invalid_found = true;
        evicted = false;
        break;
      }
    }
    
  if (!invalid_found)
    i = get_lra_entry();

  m_entry[i].m_valid = true;
  evicted_tag = m_entry[i].m_tag; //return evicted tag
  m_entry[i].m_tag = tag;

  dirty_evicted = m_entry[i].m_dirty; //return whether the evicted was dirty
  m_entry[i].m_dirty = set_dirty;

  update_access_order(i);
}

int cache_set_c::get_lra_entry() {
  return access_order[m_assoc-1];
}

/**
 * This constructor initializes a cache structure based on the cache parameters.
 * @param name - cache name; use any name you want
 * @param num_sets - number of sets in a cache
 * @param assoc - number of cache entries in a set
 * @param line_size - cache block (line) size in bytes
 *
 * @note You do not have to modify this (other than for debugging purposes).
 */
cache_base_c::cache_base_c(std::string name, int num_sets, int assoc, int line_size) {
  m_name = name;
  m_num_sets = num_sets;
  m_line_size = line_size;

  m_set = new cache_set_c *[m_num_sets];

  for (int ii = 0; ii < m_num_sets; ++ii) {
    m_set[ii] = new cache_set_c(assoc);

    // initialize tag/valid/dirty bits
    for (int jj = 0; jj < assoc; ++jj) {
      m_set[ii]->m_entry[jj].m_valid = false;
      m_set[ii]->m_entry[jj].m_dirty = false;
      m_set[ii]->m_entry[jj].m_tag   = 0;
    }
  }

  // initialize stats
  m_num_accesses = 0;
  m_num_hits = 0;
  m_num_misses = 0;
  m_num_writes = 0;
  m_num_writebacks = 0;
}

// cache_base_c destructor
cache_base_c::~cache_base_c() {
  for (int ii = 0; ii < m_num_sets; ++ii) { delete m_set[ii]; }
  delete[] m_set;
}

bool cache_base_c::evict_and_bring_new(int set_idx, int tag, int req_type, bool set_dirty) {
  bool evicted, dirty_evicted; int evicted_tag;
  m_set[set_idx]->evict_and_bring_new(tag, set_dirty, evicted, dirty_evicted, evicted_tag);
  return dirty_evicted;
}

/** 
 * This function looks up in the cache for a memory reference.
 * This needs to update all the necessary meta-data (e.g., tag/valid/dirty) 
 * and the cache statistics, depending on a cache hit or a miss.
 * @param address - memory address 
 * @param access_type - read (0), write (1), or instruction fetch (2)
 * @param is_fill - if the access is for a cache fill
 * @param return "true" on a hit; "false" otherwise.
 */
bool cache_base_c::access(addr_t address, int access_type, bool is_fill) {
  ////////////////////////////////////////////////////////////////////
  // TODO: Write the code to implement this function
  ////////////////////////////////////////////////////////////////////

  m_num_accesses++;

  bool res = false;
  int set_idx = (address / this->m_line_size) % this->m_num_sets;
  int tag = address / this->m_line_size / this->m_num_sets;

  //std::cout << address << " is accessed  at " << m_name << ", currently need_writeback is " << (need_writeback ? "true" : "false") << " and it is " << (is_fill ? "" : "not ") << "a fill inst." << std::endl;
  
  if (is_fill) {
    res = true; //assume hit when fill to prevent increment of m_num_misses
    //fill
    if (access_type == FILL_INCLUDE) {
      
      //read miss fill(bottom to top) -> always evict
      bool dirty_evicted = evict_and_bring_new(set_idx, tag, access_type, false);

      //evict_dirty = m_set[set_idx]->evict_and_bring_new(tag, false);
      if (dirty_evicted) {
        m_num_writebacks++;
        need_writeback = true;
      }
      
      //std::cout << "filling at " << m_name << "!!" << std::endl;
    } else if (access_type == FILL_EVICT) {
      //dirty eviction fill (top to bottom) -> since inclusive cache, find the victim and set it to dirty
      bool found = false;
    for (int i = 0; i < m_set[set_idx]->m_assoc; i++) {
      if (m_set[set_idx]->m_entry[i].m_valid && m_set[set_idx]->m_entry[i].m_tag == tag) {
        found = true;
        m_set[set_idx]->m_entry[i].m_dirty = 1;
        break;
      }
    }
    
    if (!found)
      std::cout << "Not found when writeback!! Error!" << std::endl;

    }
  } else {
    //access
      //READ
  if (access_type == 0 || access_type == 2) {
    for (int i = 0; i < m_set[set_idx]->m_assoc; i++) {
      if (m_set[set_idx]->m_entry[i].m_valid && m_set[set_idx]->m_entry[i].m_tag == tag) {
        //read hit
        res = true;
        m_set[set_idx]->update_access_order(i);
        m_num_hits++;
        break;
      }
    }
    
    /*
    if (!res) {
      //read miss - virtually bring the block that was not found to the cache
      if (m_set[set_idx]->evict_and_bring_new(tag, false)) { 
        m_num_writebacks++;
        need_writeback = true;
      }
      m_num_misses++;
    }
    */

  } else if (access_type == 1) {
    //WRITE
    m_num_writes++;
    for (int i = 0; i < m_set[set_idx]->m_assoc; i++) {
      if (m_set[set_idx]->m_entry[i].m_valid && m_set[set_idx]->m_entry[i].m_tag == tag) {
        //write hit
        res = true;
        m_set[set_idx]->update_access_order(i);
        m_num_hits++;
        m_set[set_idx]->m_entry[i].m_dirty = 1;
        break;
      }
    }

    /*
    if (!res) {
      //write miss
     if (m_set[set_idx]->evict_and_bring_new(tag, true)) {
      m_num_writebacks++;
      need_writeback = true;
     }
     m_num_misses++;
    }
    */
  }
  }
  if (!res) m_num_misses++;
  return res;
}

//only used for l1 cache!!
//return if invalidated data is dirty
bool cache_base_c::invalidate(addr_t address) {
  int set_idx = (address / this->m_line_size) % this->m_num_sets;
  int tag = address / this->m_line_size / this->m_num_sets;
  for (int i = 0; i < m_set[set_idx]->m_assoc; i++) {
  if (m_set[set_idx]->m_entry[i].m_valid && m_set[set_idx]->m_entry[i].m_tag == tag) {
    //if found, invalidate.
    m_set[set_idx]->m_entry[i].m_valid == false;
    if (m_set[set_idx]->m_entry[i].m_dirty) return true;
    break;
  }
}
return false;
}

/**
 * Print statistics (DO NOT CHANGE)
 */
void cache_base_c::print_stats() {
  std::cout << "------------------------------" << "\n";
  std::cout << m_name << " Hit Rate: "          << (double)m_num_hits/m_num_accesses*100 << " % \n";
  std::cout << "------------------------------" << "\n";
  std::cout << "number of accesses: "    << m_num_accesses << "\n";
  std::cout << "number of hits: "        << m_num_hits << "\n";
  std::cout << "number of misses: "      << m_num_misses << "\n";
  std::cout << "number of writes: "      << m_num_writes << "\n";
  std::cout << "number of writebacks: "  << m_num_writebacks << "\n";
}


/**
 * Dump tag store (for debugging) 
 * Modify this if it does not dump from the MRU to LRU positions in your implementation.
 */
void cache_base_c::dump_tag_store(bool is_file) {
  auto write = [&](std::ostream &os) { 
    os << "------------------------------" << "\n";
    os << m_name << " Tag Store\n";
    os << "------------------------------" << "\n";

    for (int ii = 0; ii < m_num_sets; ii++) {
      for (int jj = 0; jj < m_set[0]->m_assoc; jj++) {
        os << "[" << (int)m_set[ii]->m_entry[jj].m_valid << ", ";
        os << (int)m_set[ii]->m_entry[jj].m_dirty << ", ";
        os << std::setw(10) << std::hex << m_set[ii]->m_entry[jj].m_tag << std::dec << "] ";
      }
      os << "\n";
    }
  };

  if (is_file) {
    std::ofstream ofs(m_name + ".dump");
    write(ofs);
  } else {
    write(std::cout);
  }
}
