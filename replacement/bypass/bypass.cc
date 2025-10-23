#include "bypass.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <utility>
#include <vector>
#include <cstring>

#include "cache.h"
#include "msl/bits.h"

using namespace std;

// Global parameters
static int 
	dan_promotion_threshold = 256,
	dan_init_weight = 1,
	dan_dt1 = 55, dan_dt2 = 1024,
	dan_rrip_place_position = 0,
	dan_leaders = 34,
	dan_ignore_prefetch = 1,
	dan_use_plru = 0,
	dan_use_rrip = 0,
	dan_bypass_threshold = 1000,
	dan_record_types = 27,
	dan_sampler_assoc = 18,
	dan_predictor_index_bits = 8,
	dan_predictor_tables = 16,
	dan_counter_width = 6,
	dan_threshold = 8,
	dan_theta2 = 210,
	dan_theta = 110,
	dan_sampler_tag_bits = 16,
	dan_samplers = 80,
	dan_predictor_table_entries,
	dan_counter_min,
	dan_counter_max;

#define LLC_WAYS 1
#define	LLC_WAY	 1
#define MAX_PATH_LENGTH 16

static bool verbose = true;
static int total_bits = 0;

// trace is built here before prediction
static unsigned int trace_buffer[MAX_PATH_LENGTH+1];

// placement vector (initialized elsewhere)
int plv[3][2] = {
	{ 0, 0 },
	{ 0, 0 },
	{ 0, 0 },
};

// feature types
#define F_PC    0
#define F_TAG   1
#define F_BIAS  2
#define F_BURST 3
#define F_INS	4
#define F_LM	5
#define F_OFF	6

static const char *feature_names[] = {
"F_PC", "F_TAG", "F_BIAS", "F_BURST", "F_INS", "F_LM", "F_OFF", NULL
};

// a feature specifier
struct feature_spec {
	int     type;
	int     assoc;
	int     begin;
	int     end;
	int     which;
	int     xorpc;
};

#define MAX_SPECS       (MAX_PATH_LENGTH+1)

static feature_spec input_specs[MAX_SPECS];

static void read_specs (FILE *f) {
	char s[1000];
	assert (fgets (s, 1000, f)); sscanf (s, "%d", &dan_theta);
	if (s[0] == '[') {
                sscanf (s, "[ %d %d %d %d %d %d", &plv[0][0], &plv[0][1], &plv[1][0], &plv[1][1], &plv[2][0], &plv[2][1]);
                printf ("got [ %d %d %d %d %d %d ]\n", plv[0][0], plv[0][1], plv[1][0], plv[1][1], plv[2][0], plv[2][1]);
		fflush (stdout);
		assert (fgets (s, 1000, f)); sscanf (s, "%d", &dan_theta);
	}
	assert (fgets (s, 1000, f)); sscanf (s, "%d", &dan_threshold);
	assert (fgets (s, 1000, f)); sscanf (s, "%d", &dan_bypass_threshold);
	int i = 0;
	for (;;) {
		char *p = fgets (s, 1000, f);
		if (!p || feof (f)) break;
		char t[100];
		int p0, p1, p2, p3, p4, p5;
		sscanf (s, "{ %s %d, %d, %d, %d, %d", t, &p1, &p2, &p3, &p4, &p5);
		p0 = -1;
		for (int j=0; feature_names[j]; j++) {
			if (strstr (t, feature_names[j])) {
				p0 = j;
				break;
			}
		}
		assert (p0 != -1);
		input_specs[i].type = p0;
		input_specs[i].assoc = p1;
		input_specs[i].begin = p2;
		input_specs[i].end = p3;
		input_specs[i].which = p4;
		input_specs[i].xorpc = p5;
		i++;
	}
	dan_predictor_tables = i;
}

static feature_spec bypass_specs[] = {
{ F_TAG, 5, 2, 13, 7, 2 },
{ F_TAG, 3, 2, 13, 7, 2 },
{ F_BURST, 4, 14, 28, 9, 2 },
{ F_PC, 6, 3, 18, 10, 2 },
{ F_TAG, 2, 3, 14, 11, 2 },
{ F_OFF, 2, 0, 2, 0, 0 },
{ F_INS, 3, 11, 18, 9, 0 },
{ F_BURST, 3, 14, 28, 9, 2 },
};

static feature_spec default_single_1_specs[] = {
{ F_OFF, 15, 1, 6, 0, 1 },
{ F_PC, 7, 14, 43, 11, 0 },
{ F_PC, 16, 3, 11, 16, 1 },
{ F_INS, 16, 0, 0, 0, 1 },
{ F_OFF, 10, 0, 6, 0, 1 },
{ F_PC, 10, 1, 53, 10, 0 },
{ F_BIAS, 16, 0, 0, 0, 0 },
{ F_INS, 8, 0, 0, 0, 1 },
{ F_PC, 17, 6, 20, 0, 1 },
{ F_BURST, 6, 11, 22, 9, 0 },
{ F_LM, 9, 0, 0, 0, 0 },
{ F_PC, 17, 6, 20, 0, 1 },
{ F_INS, 16, 2, 47, 2, 0 },
{ F_INS, 17, 0, 0, 0, 1 },
{ F_PC, 16, 8, 16, 5, 0 },
{ F_PC, 17, 6, 20, 14, 1 },
};

static feature_spec default_single_2_specs[] = {
{ F_INS, 15, 7, 55, 1, 0 },
{ F_INS, 16, 0, 0, 0, 1 },
{ F_INS, 6, 13, 38, 0, 1 },
{ F_OFF, 14, 0, 7, 0, 2 },
{ F_BIAS, 17, 8, 23, 10, 3 },
{ F_BURST, 8, 1, 11, 13, 2 },
{ F_PC, 6, 5, 48, 0, 3 },
{ F_LM, 15, 16, 44, 0, 2 },
{ F_TAG, 17, 1, 32, 14, 2 },
{ F_PC, 17, 6, 20, 0, 1 },
{ F_PC, 6, 4, 11, 2, 2 },
{ F_BIAS, 13, 8, 67, 7, 2 },
{ F_OFF, 8, 1, 6, 0, 2 },
{ F_PC, 6, 5, 77, 4, 1 },
{ F_TAG, 11, 8, 19, 7, 0 },
{ F_TAG, 16, 8, 16, 0, 0 },
};

static feature_spec default_multi_3_specs[] = {
{ F_BIAS, 1, 0, 0, 0, 0 },
{ F_PC, 16, 9, 25, 9, 1 },
{ F_INS, 8, 4, 8, 7, 2 },
{ F_PC, 6, 9, 28, 12, 1 },
{ F_OFF, 14, 1, 4, 0, 3 },
{ F_LM, 7, 7, 51, 3, 1 },
{ F_PC, 10, 1, 54, 13, 3 },
{ F_PC, 10, 3, 32, 5, 1 },
{ F_PC, 14, 5, 24, 0, 1 },
{ F_OFF, 13, 4, 4, 0, 2 },
{ F_TAG, 8, 4, 47, 11, 2 },
{ F_TAG, 2, 24, 32, 0, 1 },
{ F_PC, 12, 10, 30, 0, 1 },
{ F_PC, 12, 9, 28, 0, 2 },
{ F_PC, 12, 5, 31, 2, 2 },
{ F_TAG, 8, 10, 8, 7, 1 },
};

static feature_spec default_multi_4_specs[] = {
{ F_LM, 9, 5, 17, 4, 0 },
{ F_PC, 8, 6, 8, 14, 3 },
{ F_BIAS, 13, 9, 40, 10, 3 },
{ F_OFF, 8, 2, 2, 0, 2 },
{ F_TAG, 16, 3, 14, 11, 2 },
{ F_BURST, 16, 14, 28, 9, 2 },
{ F_INS, 10, 4, 14, 3, 2 },
{ F_PC, 14, 3, 18, 10, 2 },
{ F_INS, 6, 11, 18, 9, 0 },
{ F_PC, 17, 1, 14, 5, 0 },
{ F_OFF, 11, 2, 5, 0, 0 },
{ F_OFF, 15, 0, 7, 0, 3 },
{ F_TAG, 9, 2, 13, 7, 2 },
{ F_TAG, 15, 4, 34, 3, 2 },
{ F_OFF, 10, 0, 6, 0, 1 },
{ F_PC, 11, 7, 23, 0, 2 },
};

static feature_spec *specs = NULL;

// one sampler entry
struct sdbp_sampler_entry {
	unsigned int 	
		lru_stack_position,
		tag,
		trace_buffer[MAX_PATH_LENGTH+1];
		
	int conf;

	sdbp_sampler_entry (void) {
		lru_stack_position = 0;
		tag = 0;
	};
};

struct sdbp_sampler_set {
	sdbp_sampler_entry *blocks;
	sdbp_sampler_set (void);
};

struct perceptron_predictor {
        int **tables;
        int *table_sizes;

        perceptron_predictor (void);
        ~perceptron_predictor();
        int get_prediction (uint32_t tid, int set);
        void block_is_dead (uint32_t tid, sdbp_sampler_entry *, unsigned int *, bool, int, int);
};

struct sdbp_sampler {
        sdbp_sampler_set *sets;
        int nsampler_sets;

        perceptron_predictor *pred;
        sdbp_sampler (int nsets, int assoc);
        ~sdbp_sampler();
        void access (uint32_t tid, int set, int real_set, uint64_t tag, uint64_t PC, int, uint64_t);
};

// Tree-based PseudoLRU operations
#define PLRU_LEFT(i)    ((i)*2+2)
#define PLRU_RIGHT(i)   ((i)*2+1)
#define SETBIT(z,k) ((z)|=(1<<(k)))
#define GETBIT(z,k) (!!((z)&(1<<(k))))

// ==================== bypass class implementation ====================

bypass::bypass(CACHE* cache) : bypass(cache, cache->NUM_SET, cache->NUM_WAY) {}

bypass::bypass(CACHE* cache, long sets, long ways) 
    : replacement(cache), NUM_SET(sets), NUM_WAY(ways), samp(nullptr), 
      was_burst(false), psel(0), config(1)
{
    // Determine configuration based on number of cores and sets
    num_core = 1;  // Default single core
    llc_sets = static_cast<unsigned int>(sets);
    
    // Set configuration based on sets
    if (llc_sets == 2048) {
        config = 1;
    } else if (llc_sets == 8192) {
        config = 3;  // multi-core
        num_core = 4;
    } else if (llc_sets == 32768) {  // Direct-mapped scaled
        config = 1;
        num_core = 1;
    }
    
    // Set parameters based on config
    set_parameters();
    
#if ( LLC_WAYS == 1 )
    printf("You are modeling a DM LLC\n");
    dan_sampler_assoc = 8;
    dan_bypass_threshold = 31;
    specs = bypass_specs;
    dan_samplers = 256;
    dan_predictor_tables = sizeof (bypass_specs) / sizeof (feature_spec);
    llc_sets *= 16;
    printf("Number of sets %d -> \n", llc_sets);
#endif
    
    printf ("config %d, num_core %d, llc_sets %d\n", config, num_core, llc_sets);
    
    // Calculate lognsets
    if (llc_sets == 2048) {
        lognsets = 11;
    } else if (llc_sets == 8192) {
        lognsets = 13;
    } else {
        lognsets = 13;
    }
    lognsets6 = lognsets + 6;
    
    // Initialize replacement state
    if (num_core == 4) {
        // Multi-core uses RRIP
        rrpv.resize(llc_sets);
        int rrpv_bits = 0;
        for (unsigned int i = 0; i < llc_sets; i++) {
            rrpv[i].resize(static_cast<size_t>(NUM_WAY), 3);
            rrpv_bits += 2 * NUM_WAY;
        }
        total_bits += rrpv_bits;
        if (verbose) printf ("@%d rrpv bits\n", rrpv_bits);
    } else if (num_core == 1) {
        // Single-core uses MDPP/PLRU
        int bits = 0;
        plru_bits.resize(llc_sets);
        for (unsigned int i = 0; i < llc_sets; i++) {
            plru_bits[i].resize(static_cast<size_t>(NUM_WAY - 1), false);
            bits += NUM_WAY - 1;
        }
        if (verbose) printf ("@%d $\\times$ %d = %d plru bits\n", llc_sets, NUM_WAY-1, bits);
        total_bits += bits;
    }
    
    // Per-core arrays of recent PCs
    addresses.resize(num_core);
    for (unsigned int i = 0; i < num_core; i++) {
        addresses[i].resize(MAX_PATH_LENGTH, 0);
    }
    
    // Initialize lastmiss feature
    lastmiss_bits.resize(llc_sets, false);
    if (verbose) printf ("@%d lastmiss bits\n", llc_sets);
    total_bits += llc_sets;
    
    // Initialize sampler
    samp = new sdbp_sampler(llc_sets, NUM_WAY);
    
    // Compute sizes
    int trace_bits = 0;
    for (int i = 0; i < dan_predictor_tables; i++) {
        switch (specs[i].type) {
        case F_BIAS:
        case F_BURST:
        case F_LM:
        case F_INS:
            if (specs[i].xorpc == 0 || specs[i].xorpc == 2) trace_bits += 1; 
            else trace_bits += dan_predictor_index_bits;
            break;
        case F_OFF:
            if (specs[i].xorpc == 0 || specs[i].xorpc == 2) trace_bits += (specs[i].end - specs[i].begin); 
            else trace_bits += dan_predictor_index_bits;
            break;
        default:
            trace_bits += dan_predictor_index_bits;
            break;
        }
    }
    
    int sampler_set_bits = dan_sampler_assoc * (4 + dan_sampler_tag_bits + trace_bits + 9);
    
    if (verbose) printf ("@%d trace bits for global trace buffer\n", trace_bits);
    total_bits += trace_bits;
    
    if (verbose) printf ("@%d $\\times$ %d $\\times$ %d = %d bits for sampler\n", 
                         sampler_set_bits/dan_sampler_assoc, dan_sampler_assoc, 
                         samp->nsampler_sets, sampler_set_bits * samp->nsampler_sets);
    
    int max_end = 0;
    for (int i = 0; i < dan_predictor_tables; i++) {
        if (specs[i].type == F_PC) if (specs[i].end > max_end) max_end = specs[i].end;
    }
    if (verbose) printf ("@%d $\\times$ %d $\\times$ %d = %d bits for the per-core address arrays\n", 
                         max_end, MAX_PATH_LENGTH, num_core, max_end * MAX_PATH_LENGTH * num_core);
    total_bits += max_end * MAX_PATH_LENGTH * num_core;
    int total_bits_allowed = 8 * 32768 * num_core;
    int sampler_bits = total_bits_allowed - total_bits;
    total_bits += 10;
    total_bits += 512;
    if (verbose) printf ("@can afford %d samplers\n", sampler_bits / sampler_set_bits);
    total_bits += dan_samplers * sampler_set_bits;
    if (verbose) printf ("@total bytes %d\n", total_bits / 8);
    
    leaders1 = dan_leaders * 1;
    leaders2 = dan_leaders * 2;
}

bypass::~bypass()
{
    if (samp) {
        delete samp;
    }
}

void bypass::set_parameters(void) {
    switch (config) {
    case 1: case 5:
        dan_promotion_threshold = 82;
        dan_init_weight = 1;
        dan_dt1 = 56;
        dan_dt2 = 256;
        dan_leaders = 34;
        dan_ignore_prefetch = 1;
        dan_use_plru = 0;
        dan_use_rrip = 0;
        dan_bypass_threshold = 48;
        dan_record_types = 27;
        dan_sampler_assoc = 18;
        dan_predictor_index_bits = 8;
        dan_predictor_tables = 16;
        dan_counter_width = 6;
        dan_threshold = 128;
        dan_theta = 109;
        dan_theta2 = 135;
        dan_sampler_tag_bits = 16;
        dan_samplers = 80;
        specs = default_single_1_specs;
        plv[0][0] = -15; plv[0][1] = 0;
        plv[1][0] = 35; plv[1][1] = 12;
        plv[2][0] = 44; plv[2][1] = 15;
        break;
    case 2: case 6:
        dan_promotion_threshold = 178;
        dan_init_weight = 1;
        dan_dt1 = 42;
        dan_dt2 = 48;
        dan_leaders = 34;
        dan_ignore_prefetch = 1;
        dan_use_plru = 0;
        dan_use_rrip = 0;
        dan_bypass_threshold = 48;
        dan_record_types = 27;
        dan_sampler_assoc = 18;
        dan_predictor_index_bits = 8;
        dan_predictor_tables = 16;
        dan_counter_width = 6;
        dan_threshold = 128;
        dan_theta = 109;
        dan_theta2 = 135;
        dan_sampler_tag_bits = 16;
        dan_samplers = 80;
        specs = default_single_2_specs;
        plv[0][0] = -15; plv[0][1] = 0;
        plv[1][0] = 35; plv[1][1] = 12;
        plv[2][0] = 44; plv[2][1] = 15;
        break;
    case 3:
        dan_promotion_threshold = 256;
        dan_rrip_place_position = 2;
        dan_init_weight = 1;
        dan_dt1 = 27;
        dan_dt2 = 229;
        dan_leaders = 34;
        dan_ignore_prefetch = 1;
        dan_use_plru = 0;
        dan_use_rrip = 1;
        dan_bypass_threshold = -3;
        dan_record_types = 27;
        dan_sampler_assoc = 18;
        dan_predictor_index_bits = 8;
        dan_predictor_tables = 16;
        dan_counter_width = 6;
        dan_threshold = 0;
        dan_theta = 0;
        dan_theta2 = 0;
        dan_sampler_tag_bits = 16;
        dan_samplers = 308;
        specs = default_multi_3_specs;
        plv[0][0] = -230; plv[0][1] = 1;
        plv[1][0] = 12; plv[1][1] = 2;
        plv[2][0] = 22; plv[2][1] = 3;
        break;
    case 4:
        dan_promotion_threshold = 256;
        dan_rrip_place_position = 2;
        dan_init_weight = 1;
        dan_dt1 = 100;
        dan_dt2 = 154;
        dan_leaders = 34;
        dan_ignore_prefetch = 1;
        dan_use_plru = 0;
        dan_use_rrip = 1;
        dan_bypass_threshold = -3;
        dan_record_types = 27;
        dan_sampler_assoc = 18;
        dan_predictor_index_bits = 8;
        dan_predictor_tables = 16;
        dan_counter_width = 6;
        dan_threshold = 0;
        dan_theta = 0;
        dan_theta2 = 0;
        dan_sampler_tag_bits = 16;
        dan_samplers = 337;
        specs = default_multi_4_specs;
        plv[0][0] = -111; plv[0][1] = 0;
        plv[1][0] = -110; plv[1][1] = 2;
        plv[2][0] = 20; plv[2][1] = 3;
        break;
    default: assert (0);
    }
}

void bypass::make_trace(uint32_t tid, perceptron_predictor *pred, uint32_t setIndex, 
                        uint64_t PC, uint32_t tag, uint32_t accessType, bool burst, 
                        bool insertion, bool lastmiss, unsigned int offset) {
    for (int i = 0; i < dan_predictor_tables; i++) {
        feature_spec *f = &specs[i];
        int begin_shift = f->begin;
        int end_mask = (1 << (f->end - begin_shift)) - 1;
        switch (f->type) {
        case F_PC:
            if (f->which == 0)
                trace_buffer[i] = (PC >> begin_shift) & end_mask;
            else
                trace_buffer[i] = (addresses[tid][f->which - 1] >> begin_shift) & end_mask;
            break;
        case F_TAG:
            trace_buffer[i] = (((tag << lognsets6) | (setIndex << 6)) >> begin_shift) & end_mask;
            break;
        case F_BIAS:
            trace_buffer[i] = 0;
            break;
        case F_BURST:
            trace_buffer[i] = burst;
            break;
        case F_INS:
            trace_buffer[i] = insertion;
            break;
        case F_LM:
            trace_buffer[i] = lastmiss;
            break;
        case F_OFF:
            trace_buffer[i] = (offset >> begin_shift) & end_mask;
            break;
        }
        if (f->xorpc & 1) trace_buffer[i] ^= PC;
        if (f->xorpc & 2) trace_buffer[i] ^= 2 * (access_type{accessType} == access_type::PREFETCH);
    }
}

unsigned int bypass::mm(unsigned int x, unsigned int m[]) {
    unsigned int r = 0;
    for (int i = 0; i < lognsets; i++) {
        r <<= 1;
        unsigned int d = x & m[i];
        r |= __builtin_parity(d);
    }
    return r;
}

void bypass::UpdateSampler(uint32_t setIndex, uint64_t tag, uint32_t tid, uint64_t PC, 
                           int32_t way, bool hit, uint32_t accessType, uint64_t paddr) {
    if (way >= NUM_WAY) return;
    
    if (access_type{accessType} == access_type::PREFETCH) PC ^= (0xdeadbeef + hit);
    
    if (access_type{accessType} == access_type::WRITE) {
        PC ^= 0x7e57ab1e;
        goto stuff;
    }
    
    if (dan_ignore_prefetch == 1) {
        if ((access_type{accessType} == access_type::PREFETCH) && hit) goto stuff;
    }
    
    if (setIndex < leaders1) {
        if (!hit) if (psel < 1023) psel++;
    } else if (setIndex < leaders2) {
        if (!hit) if (psel > -1023) psel--;
    }
    
    if (dan_ignore_prefetch == 2) {
        if ((access_type{accessType} == access_type::PREFETCH) && hit) goto stuff;
        
        static unsigned int le11[] = { 0x37f, 0x431, 0x71d, 0x25c, 0x719, 0x4d5, 0x4b6, 0x2ca, 0x26d, 0x64f, 0x46d };
        static unsigned int le13[] = { 0x5c5, 0xcc5, 0xb6b, 0x1bc5, 0x8b, 0x1782, 0x190, 0x15dd, 0x1af8, 0x75e, 0x4a1, 0xb4b, 0x1196 };
        unsigned int *le = (num_core == 1) ? le11 : le13;
        int set = mm(setIndex, le);
        
        if (set >= 0 && set < samp->nsampler_sets) 
            samp->access(tid, set, setIndex, tag, PC, accessType, paddr);
        
        if (dan_use_rrip) {
            was_burst = rrpv[setIndex][way] == 0;
        } else {
            // update_plru_mdpp would go here
        }
        
        make_trace(tid, samp->pred, setIndex, PC, tag, accessType, was_burst, !hit, 
                   lastmiss_bits[setIndex], paddr & 63);
        
        int conf = samp->pred->get_prediction(tid, setIndex);
        if (dan_use_rrip) {
            int position = rrpv[setIndex][way];
            if (!hit) {
                position = dan_rrip_place_position;
                if (conf >= plv[2][0]) position = plv[2][1];
                else if (conf >= plv[1][0]) position = plv[1][1];
                else if (conf >= plv[0][0]) position = plv[0][1];
            } else {
                if (conf < dan_promotion_threshold)
                    position = 0;
            }
            rrpv[setIndex][way] = position;
        } else {
            // update_plru_mdpp with conf would go here
        }
    }
    
stuff:
    bool record = false;
    if (access_type{accessType} == access_type::LOAD) if (dan_record_types & 1) record = true;
    if (access_type{accessType} == access_type::RFO) if (dan_record_types & 2) record = true;
    if (access_type{accessType} == access_type::WRITE) if (dan_record_types & 8) record = true;
    if (access_type{accessType} == access_type::PREFETCH) if (dan_record_types & 16) record = true;
    if (record) {
        memmove(&addresses[tid][1], &addresses[tid][0], (MAX_PATH_LENGTH - 1) * sizeof(unsigned int));
        addresses[tid][0] = PC;
    }
    lastmiss_bits[setIndex] = !hit;
}

int bypass::Get_Sampler_Victim(uint32_t tid, uint32_t setIndex, uint32_t current_set, 
                                uint32_t assoc, uint64_t PC, uint64_t paddr, uint32_t accessType) {
    assert(setIndex < llc_sets);
    
    int r = 0;
    if (access_type{accessType} == access_type::WRITE) return 0;
    
    uint32_t tag = paddr / (llc_sets * 64);
    make_trace(tid, samp->pred, setIndex, PC, tag, accessType, false, true, 
               lastmiss_bits[setIndex], paddr & 63);
    
    int conf = samp->pred->get_prediction(tid, setIndex);
    
    if (conf > dan_bypass_threshold) r = 1;
    
    return r;
}

long bypass::find_victim(uint32_t triggering_cpu, uint64_t instr_id, long set, 
                         const champsim::cache_block* current_set, champsim::address ip,
                         champsim::address full_addr, access_type type)
{
    uint64_t paddr = full_addr.to<uint64_t>();
    paddr &= 0x00ffffffffffffffull;
    paddr |= (((unsigned long long)triggering_cpu % 8) << 56);
    
    uint32_t set_u32 = static_cast<uint32_t>(set);
    uint32_t type_u32 = static_cast<uint32_t>(type);
    
    int result = Get_Sampler_Victim(triggering_cpu, set_u32, set_u32, NUM_WAY, 
                                    ip.to<uint64_t>(), paddr, type_u32);
    
    // If result is 1, we should bypass (return NUM_WAY)
    // If result is 0, we should use normal replacement (return 0 or LRU victim)
    if (result == 1) {
        return NUM_WAY;  // Bypass
    } else {
        // Return way 0 or implement LRU logic here
        return 0;
    }
}

void bypass::update_replacement_state(uint32_t triggering_cpu, long set, long way, 
                                      champsim::address full_addr, champsim::address ip, 
                                      champsim::address victim_addr, access_type type, uint8_t hit)
{
    uint64_t paddr = full_addr.to<uint64_t>();
    paddr &= 0x00ffffffffffffffull;
    paddr |= (((unsigned long long)triggering_cpu % 8) << 56);
    
    if (hit && (type == access_type::WRITE)) return;
    
    uint32_t set_u32 = static_cast<uint32_t>(set);
    uint32_t way_u32 = static_cast<uint32_t>(way);
    uint32_t type_u32 = static_cast<uint32_t>(type);
    uint64_t tag = paddr / (llc_sets * 64);
    
    UpdateSampler(set_u32, tag, triggering_cpu, ip.to<uint64_t>(), way_u32, hit, type_u32, paddr);
}

// ==================== Sampler implementation ====================

sdbp_sampler_set::sdbp_sampler_set(void) {
    blocks = new sdbp_sampler_entry[dan_sampler_assoc];
    for (int i = 0; i < dan_sampler_assoc; i++)
        blocks[i].lru_stack_position = i;
}

void sdbp_sampler::access(uint32_t tid, int set, int real_set, uint64_t tag, uint64_t PC, int accessType, uint64_t paddr) {
    sdbp_sampler_entry *blocks = &sets[set].blocks[0];
    unsigned int partial_tag = tag & ((1 << dan_sampler_tag_bits) - 1);
    
    int i;
    for (i = 0; i < dan_sampler_assoc; i++) 
        if (blocks[i].tag == partial_tag) {
            pred->block_is_dead(tid, &blocks[i], blocks[i].trace_buffer, false, 
                               blocks[i].conf, blocks[i].lru_stack_position);
            break;
        }
    
    bool is_fill = false;
    if (i == dan_sampler_assoc) {
        int j;
        for (j = 0; j < dan_sampler_assoc; j++)
            if (blocks[j].lru_stack_position == (unsigned int)(dan_sampler_assoc - 1)) break;
        assert(j < dan_sampler_assoc);
        i = j;
        
        pred->block_is_dead(tid, &blocks[i], blocks[i].trace_buffer, true, 
                           blocks[i].conf, dan_sampler_assoc);
        is_fill = true;
    }
    
    unsigned int position = blocks[i].lru_stack_position;
    for (int way = 0; way < dan_sampler_assoc; way++) {
        if (blocks[way].lru_stack_position < position) {
            blocks[way].lru_stack_position++;
            pred->block_is_dead(tid, &blocks[way], blocks[way].trace_buffer, true, 
                               blocks[way].conf, blocks[way].lru_stack_position);
        }
    }
    blocks[i].lru_stack_position = 0;
    
    if (is_fill) {
        blocks[i].tag = partial_tag;
    }
    
    // Record the trace
    // Access bypass member function through instance
    // We need to pass the bypass instance - but we don't have it here
    // So we'll use static trace_buffer that was already being used
    memcpy(blocks[i].trace_buffer, trace_buffer, (MAX_PATH_LENGTH + 1) * sizeof(unsigned int));
    
    blocks[i].conf = pred->get_prediction(tid, -1);
}

sdbp_sampler::sdbp_sampler(int nsets, int assoc) {
#define GET_PARAM(name,var) { char *s = getenv (name); if (!s) {if (0) fprintf (stderr, "warning: parameter %s not found in environment, default is %d\n", name, var); } else { sscanf (s, "%d", &var); if (0) fprintf (stderr, "%s=%d\n", name, var); } }
    
    GET_PARAM("DAN_PROMOTION_THRESHOLD", dan_promotion_threshold);
    GET_PARAM("DAN_INIT_WEIGHT", dan_init_weight);
    GET_PARAM("DAN_RRIP_PLACE_POSITION", dan_rrip_place_position);
    GET_PARAM("DAN_USE_RRIP", dan_use_rrip);
    GET_PARAM("DAN_THETA2", dan_theta2);
    GET_PARAM("DAN_LEADERS", dan_leaders);
    GET_PARAM("DAN_DT1", dan_dt1);
    GET_PARAM("DAN_DT2", dan_dt2);
    GET_PARAM("DAN_USE_PLRU", dan_use_plru);
    GET_PARAM("DAN_COUNTER_WIDTH", dan_counter_width);
    GET_PARAM("DAN_IGNORE_PREFETCH", dan_ignore_prefetch);
    GET_PARAM("DAN_RECORD_TYPES", dan_record_types);
    GET_PARAM("DAN_SAMPLER_ASSOC", dan_sampler_assoc);
    GET_PARAM("DAN_THRESHOLD", dan_threshold);
    GET_PARAM("DAN_BYPASS_THRESHOLD", dan_bypass_threshold);
    GET_PARAM("DAN_THETA", dan_theta);
    GET_PARAM("DAN_SAMPLERS", dan_samplers);
    GET_PARAM("DAN_PREDICTOR_TABLES", dan_predictor_tables);
    GET_PARAM("DAN_PREDICTOR_INDEX_BITS", dan_predictor_index_bits);
    
    char *s;
    s = getenv("DAN_PLACEMENT_VECTOR");
    if (s) {
        sscanf(s, "[ %d %d %d %d %d %d", &plv[0][0], &plv[0][1], &plv[1][0], &plv[1][1], &plv[2][0], &plv[2][1]);
    }
    s = getenv("DAN_SPECS");
    if (s) {
        FILE *f = fopen(s, "r");
        assert(f);
        read_specs(f);
        specs = input_specs;
        fclose(f);
        printf("read specs from \"%s\"\n", s);
        fflush(stdout);
    }
    
    dan_predictor_table_entries = 1 << dan_predictor_index_bits;
    nsampler_sets = dan_samplers;
    
    if (nsampler_sets > nsets) {
        nsampler_sets = nsets;
        fprintf(stderr, "warning: number of sampler sets exceeds number of real sets, setting nsampler_sets to %d\n", nsampler_sets);
        fflush(stderr);
    }
    
    dan_counter_max = (1 << (dan_counter_width - 1)) - 1;
    dan_counter_min = -(1 << (dan_counter_width - 1));
    fflush(stdout);
    
    pred = new perceptron_predictor();
    
    assert(nsampler_sets >= 0);
    
    sets = new sdbp_sampler_set[nsampler_sets];
}

sdbp_sampler::~sdbp_sampler() {
    if (pred) delete pred;
    if (sets) delete[] sets;
}

// ==================== Perceptron predictor implementation ====================

perceptron_predictor::perceptron_predictor(void) {
    tables = new int*[dan_predictor_tables];
    table_sizes = new int[dan_predictor_tables];
    
    for (int i = 0; i < dan_predictor_tables; i++) {
        int table_entries;
        switch (specs[i].type) {
        case F_BIAS:
        case F_BURST:
        case F_LM:
        case F_INS:
            if (specs[i].xorpc == 0) table_entries = 2; 
            else if (specs[i].xorpc == 2) table_entries = 4; 
            else table_entries = dan_predictor_table_entries;
            break;
        case F_OFF:
            if (specs[i].xorpc == false) table_entries = 1 << (specs[i].end - specs[i].begin); 
            else table_entries = dan_predictor_table_entries;
            break;
        default:
            table_entries = dan_predictor_table_entries;
        }
        table_sizes[i] = table_entries;
        tables[i] = new int[table_entries];
        for (int j = 0; j < table_entries; j++) tables[i][j] = dan_init_weight;
        if (verbose) printf("@table %d: %d $\\times$ %d = %d bits\n", i, 6, table_entries, 6 * table_entries);
        total_bits += 6 * table_entries;
    }
}

perceptron_predictor::~perceptron_predictor() {
    if (tables) {
        for (int i = 0; i < dan_predictor_tables; i++) {
            if (tables[i]) delete[] tables[i];
        }
        delete[] tables;
    }
    if (table_sizes) delete[] table_sizes;
}

void perceptron_predictor::block_is_dead(uint32_t tid, sdbp_sampler_entry *block, 
                                         unsigned int *trace_buffer_param, bool d, int conf, int pos) {
    bool prediction = conf >= dan_threshold;
    bool correct = prediction == d;
    
    printf("what is d %d\n", d);
    
    bool do_train = false;
    if (conf < 0) {
        if (conf > -dan_theta2) do_train = true;
    } else {
        if (conf < dan_theta) do_train = true;
    }
    if (!correct) do_train = true;
    if (!do_train) return;
    
    for (int i = 0; i < dan_predictor_tables; i++) {
        if (d) if (specs[i].assoc != pos) continue;
        if (!d) if (specs[i].assoc <= pos) continue;
        
        int *c = &tables[i][trace_buffer_param[i] % table_sizes[i]];
        
        if (d) {
            if (*c < dan_counter_max) { 
                (*c)++;
                printf("increased counter for table %d\n", i);
            }
        } else {
            if (*c > dan_counter_min) {
                printf("decreased counter for table %d\n", i);
                (*c)--;
            }
        }
    }
}

int perceptron_predictor::get_prediction(uint32_t tid, int set) {
    int conf = 0;
    
    for (int i = 0; i < dan_predictor_tables; i++) {
        int val = tables[i][trace_buffer[i] % table_sizes[i]];
        conf += val;
    }
    
    if (conf > 255) conf = 255;
    if (conf < -256) conf = -256;
    return conf;
}
