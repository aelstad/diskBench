#ifndef DISKBENCH_H_
#define DISKBENCH_H_

/*
 * dishBench.h
 *
 * Part of diskBench - IO bandwidth measurement
 *
 * Copyright (C) 2010-2011  Amund Elstad <amund.elstad@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "apr_general.h"
#include "apr_ring.h"
#include "apr_atomic.h"
#include "apr_thread_proc.h"
#include "apr_thread_cond.h"
#include "apr_random.h"
#include "apr_strings.h"
#include "apr_time.h"

#ifndef apr_time_from_msec
#define apr_time_from_msec(msec)   ((apr_time_t)(msec) * 1000)
#endif

/* Everything is based on 1024 */
#define K 1024
#define VERSION "diskBench 1.0rc3 (C) Amund Elstad 2007-2012, built " __DATE__ " " __TIME__

struct platform_file;
struct async_platform_queue;
struct io_workload;

struct async_queue;
struct async_queue_entry;
struct io_worker;


typedef apr_status_t (*iocallback_t)(struct async_queue *queue, struct async_queue_entry *ioop);

struct io_request {
	struct async_queue_entry *queue;

	int64_t offset;
	uint64_t size;

	void *buf;
	uint64_t bufsize;

	apr_time_t pre_submission;
	apr_time_t post_submission;
	apr_time_t completed;

	int write;
};

struct async_queue_entry {
	APR_RING_ENTRY(async_queue_entry) link;

    struct io_request request;

	iocallback_t callback;
};

struct async_queue {
	apr_pool_t *pool;

	struct io_workload *workload;

	uint32_t total;
	uint32_t active;
	uint32_t free;

	struct async_queue_entry *ioaqes;
	struct async_ioop_ring *ready;

	struct async_platform_queue *platform_queue;
};

struct io_workload_generator {
    apr_status_t (*fill_request)(struct io_workload_generator *workload_generator, struct io_request *request);
    apr_status_t (*reset)(struct io_workload_generator *workload_generator, struct io_workload *workload, uint64_t reqsize);
    uint64_t (*max_io_size)(struct io_workload_generator *workload_generator);
    uint64_t (*weighted_io_size)(struct io_workload_generator *workload_generator);

    struct io_workload *workload;

    /* opaque-value - ie position for sequential generator */
    void *generator_data;
};

struct io_worker_options {
    struct platform_ops *platform_ops;

    int write_random;
    int keep_files;
    int validate_existing;
    apr_time_t max_execution_time;
    apr_time_t max_preparation_time;

    apr_pool_t *pool;
    apr_array_header_t *statistics_array;

    char *xml_output;
};

struct io_worker {
    struct io_worker_options *options;

    struct platform_file *file;
    struct io_workload *workload;

    void *buf;
	char *filename;

	uint64_t filesize;
	uint64_t bufsize;
	uint64_t iolimit;
	uint64_t configured_iolimit;
	uint64_t last_integrity_written_offset;

	uint64_t random_seed;

    int truncate_file;

    char *description;
};


struct io_workload {
    struct io_worker *worker;
	struct io_workload_generator *request_generator;
	struct io_workload_generator *template_generator;
    apr_array_header_t *reqsizes;
    apr_array_header_t *depths;

	uint64_t submitted_bytes;

    uint64_t read_requests;
    uint64_t read_bytes;
    apr_time_t read_elapsed;
    apr_time_t read_min_latency;
    apr_time_t read_max_latency;

    uint64_t write_requests;
    uint64_t write_bytes;
    apr_time_t write_elapsed;
    apr_time_t write_min_latency;
    apr_time_t write_max_latency;

    apr_time_t start_time;
    apr_time_t end_time;

	int queue_depth;
	int max_active;

    char *description;
};

struct io_statistics {
    char *description;

    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t read_requests;
    uint64_t write_requests;

    apr_time_t elapsed;

    double accumulated_weight;
    double accumulated_weighted_bytes_per_second;

    double min_throughput;
    double max_throughput;

    int max_active;

    apr_array_header_t *lines;
};

struct io_statistics_line {
    double weight;

	apr_time_t read_elapsed;
	apr_time_t write_elapsed;

	uint64_t read_requests;
	uint64_t write_requests;
	uint64_t total_requests;

	uint64_t bytes_read;
	uint64_t bytes_written;
	uint64_t total_bytes;

	apr_time_t min_read_latency;
	apr_time_t min_write_latency;
	apr_time_t max_read_latency;
	apr_time_t max_write_latency;

	apr_time_t total_elapsed;
	apr_time_t min_latency;
	apr_time_t avg_latency;
	apr_time_t max_latency;

	double bytes_per_io;
	double bytes_per_second;
    double weighted_bytes_per_second;
	double iops;
};

APR_RING_HEAD(async_ioop_ring, async_queue_entry);


struct platform_ops {
    apr_status_t (*create_io_buffer)(void **buf, uint64_t size);

	apr_size_t (*get_page_size)();
    apr_size_t (*get_min_iosize)();


	apr_status_t (*file_open)(char *filename, uint64_t *length, double freespace_utilization,
                           int *file_truncated, struct platform_file **rv);
	apr_status_t (*file_truncate)(struct platform_file *the_file, uint64_t *length);
	apr_status_t (*file_close)(struct platform_file *the_file);
	apr_status_t (*file_flush)(struct platform_file *the_file);

	apr_status_t (*queue_create)(struct async_queue *queue);
	apr_status_t (*queue_destroy)(struct async_queue *queue);

	apr_status_t (*queue_read)(struct async_queue *queue, struct async_queue_entry *ioop);
	apr_status_t (*queue_write)(struct async_queue *queue, struct async_queue_entry *ioop);

	apr_status_t (*queue_wait)(struct async_queue *queue, int block);
};

extern struct platform_ops *platform_ops;
apr_status_t generic_queue_notify(
	struct async_queue *queue,
	struct async_queue_entry *ioop);


/* Fast 64-bit random generator */
static inline uint64_t random_uint64_t(uint64_t *seed)
{
    uint64_t value = *seed;

    *seed^=(*seed<<13);
    *seed^=(*seed>>7);
    return (*seed^=(*seed<<17));
}

/* Helper function */
static inline apr_time_t min_time(apr_time_t a, apr_time_t b) {
    return a <= b ? a : b;
}

/* Helper function */
static inline apr_time_t max_time(apr_time_t a, apr_time_t b) {
    return a >= b ? a : b;
}


/*
 * Create async IO queue
 */
apr_status_t generic_queue_create(struct io_workload *workload,
                                         int queue_depth,
                                         struct async_queue **queue);


/*
 * Blocking wait for IO completion if *events > 0 or queue has
 * no free slots. Non-blocking wait for IO completion otherwise
 */
apr_status_t generic_queue_wait(struct async_queue *queue, int *events);

/*
 * Blocking wait for completion of all pending IO
 */
apr_status_t generic_queue_barrier(struct async_queue *queue);

/*
 * Queue a read
 */
apr_status_t generic_queue_read(struct async_queue *queue, struct async_queue_entry *ioop);

/*
 * Queue a write
 */
apr_status_t generic_queue_write(struct async_queue *queue, struct async_queue_entry *ioop);


/*
 * Destroy IO queue
 */
apr_status_t generic_queue_destroy(struct async_queue *queue);


/*
 * Create a sequential request generator
 */
apr_status_t sequential_request_generator_factory(
    struct io_workload_generator **request_generator,
    int write
);

/*
 * Create a random request generator
 */
apr_status_t random_request_generator_factory(
    struct io_workload_generator **request_generator,
    int write);


#endif /*DISKBENCH_H_*/
