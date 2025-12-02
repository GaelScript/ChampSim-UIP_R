#ifndef REPLACEMENT_BYPASS_H
#define REPLACEMENT_BYPASS_H

#include <vector>
#include <map>
#include <memory>

#include "cache.h"
#include "modules.h"

// Forward declarations
struct sdbp_sampler;
struct perceptron_predictor;
struct sdbp_sampler_entry;
struct feature_spec;

class bypass : public champsim::modules::replacement
{
private:
  long NUM_SET;
  long NUM_WAY;
  
  // State variables
  std::vector<std::vector<bool>> plru_bits;
  std::vector<bool> lastmiss_bits;
  std::vector<std::vector<unsigned char>> rrpv;
  std::vector<std::vector<unsigned int>> addresses;

  std::unique_ptr<sdbp_sampler> samp;

  unsigned int llc_sets;
  unsigned int num_core;
  
  int lognsets6;
  int lognsets;
  
  bool was_burst;
  int psel;
  unsigned int leaders1;
  unsigned int leaders2;
  
  // Configuration
  int config;

  // Global parameters moved to class members
  int dan_promotion_threshold = 256;
  int dan_init_weight = 1;
  int dan_dt1 = 55;
  int dan_dt2 = 1024;
  int dan_rrip_place_position = 0;
  int dan_leaders = 34;
  int dan_ignore_prefetch = 1;
  int dan_use_plru = 0;
  int dan_use_rrip = 0;
  int dan_bypass_threshold = 1000;
  int dan_record_types = 27;
  int dan_sampler_assoc = 18;
  int dan_predictor_index_bits = 8;
  int dan_predictor_tables = 16;
  int dan_counter_width = 6;
  int dan_threshold = 8;
  int dan_theta2 = 210;
  int dan_theta = 110;
  int dan_sampler_tag_bits = 16;
  int dan_samplers = 80;
  int dan_predictor_table_entries;
  int dan_counter_min;
  int dan_counter_max;

  feature_spec *specs = nullptr;
  unsigned int trace_buffer[17]; // MAX_PATH_LENGTH + 1
  int plv[3][2] = {{0,0}, {0,0}, {0,0}};

  void read_specs(FILE *f);

  // Helper functions
  void set_parameters();
  void make_trace(uint32_t tid, perceptron_predictor *pred, uint32_t setIndex, 
                  uint64_t PC, uint32_t tag, uint32_t accessType, bool burst, 
                  bool insertion, bool lastmiss, unsigned int offset);
  unsigned int mm(unsigned int x, unsigned int m[]);
  void UpdateSampler(uint32_t setIndex, uint64_t tag, uint32_t tid, uint64_t PC, 
                     int32_t way, bool hit, uint32_t accessType, uint64_t paddr);
  int Get_Sampler_Victim(uint32_t tid, uint32_t setIndex, uint32_t current_set, 
                         uint32_t assoc, uint64_t PC, uint64_t paddr, uint32_t accessType);

  friend struct sdbp_sampler;
  friend struct perceptron_predictor;

public:
  explicit bypass(CACHE* cache);
  bypass(CACHE* cache, long sets, long ways);
  ~bypass();

  // Enable move (defined in .cc)
  bypass(bypass&&);
  bypass& operator=(bypass&&);

  long find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, 
                   const champsim::cache_block* current_set, champsim::address ip,
                   champsim::address full_addr, access_type type);
  
  void update_replacement_state(uint32_t triggering_cpu, long set, long way, 
                                champsim::address full_addr, champsim::address ip, 
                                champsim::address victim_addr, access_type type, uint8_t hit);
};

#endif
