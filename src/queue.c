/*
  * queue.c
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
#include "diskBench.h"

#define NONRANDOM_CONSTANT UINT64_C(0xABCDEF9876543210)

static void integrity_error(struct io_request *request)
{
    printf("ERROR: Data integrity error at offset %"APR_UINT64_T_FMT ". Check your hardware/software!\n", (apr_uint64_t) request->offset);
    exit(1);
}

static apr_status_t read_complete(struct async_queue *queue, struct async_queue_entry *entry)
{
	apr_time_t elapsed;
	struct io_request *request = &entry->request;
	struct io_workload *workload = queue->workload;
	uint64_t tmp;
	uint64_t seed;
	uint64_t *buf;
	uint64_t i;
	int64_t off;
	uint64_t end;

	request->completed = apr_time_now();
	elapsed = request->completed - request->pre_submission;

    if(workload->read_requests == 0) {
        workload->read_min_latency = elapsed;
        workload->read_max_latency = elapsed;
    } else {
        if(elapsed < workload->read_min_latency)
            workload->read_min_latency = elapsed;

        if(elapsed > workload->read_max_latency)
            workload->read_max_latency  = elapsed;
    }

    workload->read_requests += 1;
	workload->read_bytes += request->size;
	workload->read_elapsed += elapsed;

	buf = (uint64_t*) request->buf;
	i=0;
	off = request->offset;
    while(i < request->size && off < workload->worker->last_integrity_written_offset) {
        tmp = *buf;
        end = i + 512;
        if(tmp != off) {
            integrity_error(request);
        }
        ++buf;
        i += sizeof(uint64_t);
        if(workload->worker->options->write_random) {
            seed = *buf;
            i += sizeof(uint64_t);
            ++buf;
            while(i < end) {
                tmp = random_uint64_t(&seed);
                if(*buf != tmp) {
                    printf("Random mismatch");
                    integrity_error(request);
                }
                i += sizeof(uint64_t);
                ++buf;
            }
        } else {
            while(i < end) {
                if(*buf != NONRANDOM_CONSTANT) {
                   integrity_error(request);
                }
                i += sizeof(uint64_t);
                ++buf;
            }
        }
        off = request->offset + i;
    }

#ifdef DEBUG
	printf("read_complete %"APR_UINT64_T_FMT " %" APR_UINT64_T_FMT "\n", (apr_uint64_t) request->offset, (apr_uint64_t) request->size);
#endif
	return APR_SUCCESS;
}

static apr_status_t write_complete(struct async_queue *queue, struct async_queue_entry *entry)
{
	apr_time_t elapsed;
	struct io_request *request = &entry->request;
	struct io_workload *workload = queue->workload;

	request->completed = apr_time_now();
	elapsed = request->completed - request->pre_submission;

    if(workload->write_requests == 0) {
        workload->write_min_latency = elapsed;
        workload->write_max_latency = elapsed;
    } else {
        if(elapsed < workload->write_min_latency)
            workload->write_min_latency = elapsed;

        if(elapsed > workload->write_max_latency)
            workload->write_max_latency  = elapsed;
    }

    workload->write_requests += 1;
	workload->write_bytes += request->size;
	workload->write_elapsed += elapsed;

    if(request->offset == workload->worker->last_integrity_written_offset)
        workload->worker->last_integrity_written_offset = request->offset+request->size;

#ifdef DEBUG
	printf("write_complete %"APR_UINT64_T_FMT " %" APR_UINT64_T_FMT "\n", (apr_uint64_t) request->offset, (apr_uint64_t) request->size);
#endif

	return APR_SUCCESS;
}


/* Callback when IO complete */
apr_status_t generic_queue_notify(
	struct async_queue *queue,
	struct async_queue_entry *ioop)
{
	apr_status_t rv;

	rv = ioop->callback(queue, ioop);
	APR_RING_INSERT_TAIL(queue->ready, ioop, async_queue_entry, link);
	queue->free = queue->free + 1;
	queue->active = queue->active - 1;

	return rv;
}

/*
 * Blocking wait for IO completion if *events > 0 or queue has
 * no free slots. Non-blocking wait for IO completion otherwise
 */
apr_status_t generic_queue_wait(struct async_queue *queue, int *events)
{
	int oldActive;
	int received=0;
	apr_status_t rv;
	while(queue->active > 0) {
		oldActive = queue->active;
		rv = queue->workload->worker->options->platform_ops->queue_wait(queue, received<*events || queue->free == 0);
		assert(rv == APR_SUCCESS);
		if(oldActive - queue->active <= 0)
			break;

		received += (oldActive - queue->active);
	}

	*events = received;
	return APR_SUCCESS;

}

/*
 * Blocking wait for completion of all pending IO
 */
apr_status_t generic_queue_barrier(struct async_queue *queue)
{
	int events = queue->active;
	return generic_queue_wait(queue, &events);
}

apr_status_t generic_queue_write(struct async_queue *queue, struct async_queue_entry *ioop)
{
	uint64_t *buf;
	uint64_t i, end;
	int64_t off;
	struct io_worker *worker = queue->workload->worker;
    struct io_request *request = &ioop->request;

	buf = (uint64_t*) request->buf;
	i=0;
	off = request->offset;
    while(i < request->size) {
        end = i + 512;
		*buf = request->offset + i;
		i += sizeof(uint64_t);
        ++buf;

        if(worker->options->write_random) {
	        *buf = worker->random_seed;
	        ++buf;
            i += sizeof(uint64_t);
            while(i < end) {
                *buf = random_uint64_t(&worker->random_seed);
                i += sizeof(uint64_t);
                ++buf;
            }
        } else {
            while(i < end) {
                *buf = NONRANDOM_CONSTANT;
                i += sizeof(uint64_t);
                ++buf;
            }
        }
        off = request->offset + i;
    }

#ifdef DEBUG
    printf("generic_queue_write: %"APR_UINT64_T_FMT " %" APR_UINT64_T_FMT"\n", (apr_uint64_t) ioop->request.offset, (apr_uint64_t) ioop->request.size);
#endif
    ioop->callback = &write_complete;

	return queue->workload->worker->options->platform_ops->queue_write(queue, ioop);
}



apr_status_t generic_queue_read(struct async_queue *queue, struct async_queue_entry *ioop)
{
#ifdef DEBUG
	printf("generic_queue_read: %"APR_UINT64_T_FMT " %" APR_UINT64_T_FMT"\n", (apr_uint64_t) ioop->request.offset, (apr_uint64_t) ioop->request.size);
#endif

    ioop->callback = &read_complete;;
	return queue->workload->worker->options->platform_ops->queue_read(queue, ioop);
}

apr_status_t generic_queue_create(struct io_workload *workload,
                                         int queue_depth,
                                         struct async_queue **queue)
{
	apr_status_t rv;
	int i=0;
	uint64_t bufsize;
	struct io_worker *worker = workload->worker;

	*queue = malloc(sizeof(struct async_queue));
	apr_pool_create(&((*queue)->pool), NULL);
	(*queue)->workload = workload;
	(*queue)->total = workload->queue_depth;
	(*queue)->free = workload->queue_depth;
	(*queue)->active = 0;

	(*queue)->ready = apr_pcalloc((*queue)->pool, sizeof(struct async_ioop_ring));
	(*queue)->ioaqes = apr_pcalloc((*queue)->pool, sizeof(struct async_queue_entry)*workload->queue_depth);
	APR_RING_INIT((*queue)->ready, async_queue_entry, link);

    bufsize = worker->bufsize / queue_depth;
    assert(bufsize >= worker->options->platform_ops->get_page_size());
    /* align bufsize down to nearest pagesize */
    bufsize = bufsize - bufsize % worker->options->platform_ops->get_page_size();

	for(i = 0; i < workload->queue_depth; ++i) {
		APR_RING_INSERT_TAIL((*queue)->ready, &((*queue)->ioaqes[i]), async_queue_entry, link);
        (*queue)->ioaqes[i].request.buf = worker->buf + i*bufsize;
        (*queue)->ioaqes[i].request.bufsize = bufsize;
	}

	rv = worker->options->platform_ops->queue_create(*queue);
	return rv;
}

apr_status_t generic_queue_destroy(struct async_queue *queue)
{
	platform_ops->queue_destroy(queue);
	apr_pool_destroy(queue->pool);
	free(queue);
	return APR_SUCCESS;
}
