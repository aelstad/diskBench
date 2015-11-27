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
	uint64_t latency_histogram[18*64];
};
 
 
 static inline uint64_t latency_at_histogram_idx(int idx)
{
	int idx1 = idx >> 6;	
	int idx2 = idx - (idx1<<6);
	
	if(idx1 > 17) {
		idx1 = 17;
		idx2 = 63;
	}
	
	if(idx1 > 1) {
		idx2 <<= (idx1-1);
		/* move to middle of bucket (rounded down) */
		idx2 += (1<<(idx1-2))-1;
	}
	
	if(idx1 == 0) {
		return idx2;
	} else {
		return (UINT64_C(64) <<  (idx1-1))  + idx2;
	}
}

static inline int histogram_idx_for_latency(uint64_t latency) 
{
	int i=0;
	uint64_t tmp = latency;
	while(tmp >= 64 && i < 17) {
		tmp >>= 1;
		++i;
	} 
	
	if(i < 2) {
		return  (int) latency;
	} else if(tmp >= 64) {
		return i*64 + 63;
	} else {
		return i*64 + ((latency >>  (i-1) )&(63));
	}
}
