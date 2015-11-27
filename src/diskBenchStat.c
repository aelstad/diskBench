#include "diskBenchStat.h"

/**
 * Returns the avg. latency of histogram bucket [idx]
 */
 static  apr_time_t get_avg_latency_at_histogram_idx(
	int idx)
{
	int idx1 = idx >> 6;	
	int idx2 = idx - (idx1<<6);
	
	apr_time_t min_latency;
	apr_time_t max_latency;
	
	if(idx1 > 17) {
		idx1 = 17;
		idx2 = 63;
	}
	
	if(idx1 > 1) {
		idx2 <<= (idx1-1);
	}

	if(idx1 == 0) {
		min_latency = idx2;
		max_latency = min_latency+1;
	} else {
		min_latency = ((UINT64_C(64) <<  (idx1-1))  + idx2);			
		max_latency = min_latency + (UINT64_C(1)<<(idx1-1));
	}
	
	return ((max_latency+min_latency)>>1);	
}


static void add_latency_to_histogram(
	struct io_request_counter *counter,
	apr_time_t latency) 
{
	int i=0;
	int bucket;

	apr_time_t tmp = latency;
	while(tmp >= 64 && i < 17) {
		tmp >>= 1;
		++i;
	} 
	
	if(i < 2) {
		bucket = (int) latency;
	} else if(tmp >= 64) {
		bucket =  i*64 + 63;
	} else {
		bucket =  i*64 + ((latency >>  (i-1) )&(63));
	}
	counter->latency_histogram[bucket] = counter->latency_histogram[bucket] + 1;
}


void add_request_to_counter(struct io_request_counter *counter, struct io_request_stat *stat)
{	
	uint64_t tmp;
	
	double delta;
	int i;
	
	uint64_t total_latency = (stat->io_complete - stat->io_submit);
	
	
	counter->requests = counter->requests + 1;
	if(counter->requests == 1) {
		counter->min_latency = total_latency;
		counter->max_latency = total_latency;
		counter->mean_latency = (double) total_latency;
		counter->total_latency = total_latency;
		counter->bytes = bytes;
	} else {
		/* update variance */
		delta = ((double) total_latency) - counter->mean_latency;
		counter->mean_latency = counter->mean_latency + delta / (double) (counter->requests);
		counter->m2_latency = counter->m2_latency + delta  * ((double) total_latency - counter->mean_latency);		
		
		/* update min/max latency */
		if(total_latency < counter->min_latency) {
			counter->min_latency = total_latency;
		}  		
		if(total_latency > counter->max_latency) {
			counter->max_latency = total_latency;
		}
		
		/* update counter*/
		counter->bytes = counter->bytes + bytes;
		counter->total_latency = counter->total_latency + total_latency;			
	} 	

	/* update latency histogram bins */
	apr_time_t tmp=total_latency;
	int i=0;
	while(tmp > 64 && i < 18) {
		tmp >>= 1;
		++i;
	} 
	int bin = i*64 + (total_latency>>(i<1 ? i : i-1)) & (64-1); 
	counter->latency_histogram[bin] = counter->latency_histogram[bin] + 1;		
}

void start_request_counter(struct io_workload *workload, struct io_request_counter *counter) 
{
	memset(counter, 0, sizeof(struct io_request_counter));
	counter->start_time = workload->start_time;
}

void finish_request_counter(struct io_workload *workload, struct io_request_counter *counter) 
{
	counter->sample_time = workload->end_time;
	counter->wall_elapsed = counter->sample_time - counter->start_time;
	if(counter->requests > 1) {
		counter->variance_latency = counter->m2_latency/(counter->requests-1);
		counter->mean_latency = ((double) counter->total_latency) / ((double) counter->requests);
	} 	
	if(counter->requests > 0) {
		counter->bytes_per_io = ((double) counter->bytes) / ((double) counter->requests); 
		counter->bytes_per_second = (((double) counter->bytes) / ((double) counter->end_time - counter->start_time))*apr_time_from_sec(1);
		
		counter->iops = (((double) counter->requests) / ((double) counter->end_time - counter->start_time))*apr_time_from_sec(1);	
		
		counter->max_active = workload->max_active;
	}
}



struct io_request_counter combine_request_counters(struct io_request_counter *a, struct io_request_counter *b)
{
	struct io_request_counter rv;
	
	int overlap;
	int i;
	double combined_mean;
	memset(&rv, 0, sizeof(struct io_request_counter));
	if(a->requests > 0 && b->requests > 0) {	
		rv.total_latency = a->total_latency + b->total_latency;
		rv.bytes = a->bytes + b->bytes;
		rv.requests = a->requests + b->requests;
		
		if(a->min_latency < b->min_latency) {
			rv.min_latency = a->min_latency;
		} else {
			rv.min_latency = b->min_latency;
		}
		
		if(a->max_latency > b->max_latency) {
			rv.max_latency = a->max_latency;
		} else {
			rv.max_latency = b->max_latency;
		}
		/* combine mean  */
		combined_mean = (a->requests*a->mean_latency  + b->requests*b->mean_latency)/(a->requests + b->requests);
		rv.mean_latency = combined_mean;
		
		/* combine variance  */
		rv.variance_latency = (a->requests*(a->variance_latency+(a->mean_latency-combined_mean)*(a->mean_latency-combined_mean)) 
			+ b->requests*(b->variance_latency+(b->mean_latency-combined_mean)*(b->mean_latency-combined_mean)))
			/ (a->requests + b->requests);
			
		/* combine histogram */
		for(i=0; i < 18*64; ++i)
		{	
			rv.latency_histogram[i] = a->latency_histogram[i] + b->latency_histogram[i];
		}		
		rv.start_time = min_time(a->start_time,b->start_time);
		rv.end_time = max_time(a->end_time,b->end_time);
		
		/* do a and b overlap ? */
		overlap = a->start_time <= b->sample_time && a->end_time >= b->sample_time;

		rv.bytes_per_io = ((double)rv.bytes) / ((double) rv.requests); 
		rv.bytes_per_second = (((double) rv.bytes) / ((double) rv.wall_elapsed))*apr_time_from_sec(1);
		rv.iops = (((double) rv.requests) / ((double) rv.wall_elapsed))*apr_time_from_sec(1);	
				
			
		if(overlap) {
			/* use average weight */
			rv.weight = (a->weight + b->weight)/2;
			rv.weighted_bytes_per_second = rv.bytes_per_second * rv.weight;			 
		} else {
			/* use sum of weights */
			rv.weight = a->weight + b->weight;					
			rv.weighted_bytes_per_second = (a->weighted_bytes_per_second + b->weighted_bytes_per_second);			
		}
						
		if(overlap) {
			rv.max_active = a->max_active + b->max_active;
		} else {
			rv.max_active = max(a->max_active, b->max_active);
		}
	} else if(a->requests > 0) {
		rv = *a;
	} else {
		rv = *b;
	}

	return rv;
}



apr_time_t get_latency_percentile(struct io_request_counter *counter, dobule percentile) 
{
	uint64_t total=0;
	uint64_t accumulated=0;
	for(i=0; i < NUM_LATENCY_HISTOGRAM_BUCKETS; ++i) {
		total += counter->latency_histogram[i];
	}
	
	for(i=0; i < NUM_LATENCY_HISTOGRAM_BUCKETS; ++i) {
		accuumulated += counter->latency_histogram[i];
		if(((100.0*(double) accumulated)/((double) total)) >= percentile) {
				break;
		}
	}
	
	return get_avg_latency_at_histogram_idx(i);
}
