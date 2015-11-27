#ifndef DISKBENCH_STAT_H_
#define DISKBENCH_STAT_H_

#include "apr_time.h"

#define NUM_LATENCY_HISTOGRAM_BUCKETS (18*64)

struct io_request_stat {
	uint64_t reqsize;
	
	apr_time_t io_presubmit;
	apr_time_t io_postsubmit;
	apr_time_t io_complete;
};

struct io_request_counter {
	apr_time_t start_time;
	apr_time_t sample_time;

	uint64_t requests;		
	uint64_t bytes;
		
	apr_time_t total_latency;		
	apr_time_t min_latency;
	apr_time_t max_latency;
			
	double mean_latency;
	/* used in online variance calculation */
	double m2_latency;
	
	double variance_latency;
	
    /* Histogram bins counting the number of requests completed with latency */    
	uint64_t latency_histogram[NUM_LATENCY_HISTOGRAM_BUCKETS];
	
	/* Fields computed on finish */	
	double bytes_per_io;
	double bytes_per_second;
	
	double iops;	
	
	apr_time_t wall_elapsed;		
};


struct io_workload 
{
	char *description;
	char *filename;
		
	uint64_t iolimit;
	uint64_t filesize;	
	
	uint32_t depth;
	uint64_t reqsize;
	
	struct io_request_counter read_counter;
	struct io_request_counter write_counter;
	
	struct io_request_counter combined_counter;
};

struct io_aggregate_statistics
{
	struct io_
};

void add_request_to_counter(struct io_request_counter *counter, struct io_request_stat *stat);


apr_time_t get_latency_percentile(struct io_request_counter *counter, double percentile);


struct io_request_counter 
	combine_request_counters(struct io_request_counter *a, struct io_request_counter *b);

void start_request_counter(struct io_workload *workload, struct io_request_counter *counter);
	
void finish_request_counter(struct io_workload *workload, struct io_request_counter *counter);

#endif
