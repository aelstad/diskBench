/*
  * mixed_workload.c
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

static uint32_t blocksizes[13] = {
        512,
        1024,
        2*1024,
        4*1024,
        8*1024,
        16*1024,
        32*1024,
        64*1024,
        128*1024,
        256*1024,
        512*1024,
        1024*1024,
        2048*1024
};

 struct mixed_request_generator_data {

    uint32_t sequential_blocksize_prob[13];
    uint32_t random_blocksize_prob[13];

    int min_blocksize_idx;
    int blocksizes;

    /* seq write forward */
    uint64_t seq_pos1;
    /* seq write backward */
    uint64_t seq_pos2;
    /* seq read forward */
    uint64_t seq_pos3;
    /* seq read backward */
    uint64_t seq_pos4;
};

static uint32_t get_random_iosize(struct mixed_request_generator_data *data, uint32_t random_low)
{
    int start = data->min_blocksize_idx;
    int end = start + data->blocksizes - 1;
    int i;
    uint32_t cmp;
    for(i=start; i < end; ++i)
    {
        cmp = data->random_blocksize_prob[i];
        if(random_low < cmp)
            return blocksizes[i];
    }
    return blocksizes[end];
}

static uint32_t get_sequential_iosize(struct mixed_request_generator_data *data, uint32_t random_low)
{
    int start = data->min_blocksize_idx;
    int end = start + data->blocksizes - 1;
    int i;
    uint32_t cmp;
    for(i=start; i < end; ++i)
    {
        cmp = data->sequential_blocksize_prob[i];
        if(random_low < cmp)
            return blocksizes[i];
    }
    return blocksizes[end];
}

static apr_status_t mixed_request_generator_fill_request(
    struct io_workload_generator *workload_generator,
    struct io_request *request
)
{
    struct mixed_request_generator_data *data = (struct mixed_request_generator_data*) workload_generator->generator_data;
    uint64_t *random_seed = &workload_generator->workload->worker->random_seed;

	uint64_t random_base = random_uint64_t(random_seed);
	uint32_t random_low = random_base&UINT32_MAX;
	uint32_t iosize;

	if(random_low < 0.75*UINT32_MAX) {
	    request->write = 0;
        if((random_low & 0x1F) == 0) {
            /* sequential read */
            iosize = get_sequential_iosize(data, random_base>>32);
            request->size = iosize;
            if(random_low & 1) {
                /* forward */
                if(data->seq_pos1 + iosize > workload_generator->workload->worker->filesize)
                    data->seq_pos1 = 0;

                request->offset = data->seq_pos1;
                data->seq_pos1 += iosize;
            } else {
                /* backward */
                if(data->seq_pos2 < iosize)
                    data->seq_pos2 = workload_generator->workload->worker->filesize;

                data->seq_pos2 -= iosize;
                request->offset = data->seq_pos2;
            }
        } else {
            /* random read */
            iosize = get_random_iosize(data, random_base>>32);
            request->offset = random_base % (workload_generator->workload->worker->filesize / iosize);
            request->offset = request->offset  * iosize;
            request->size = iosize;
        }
	} else {
        request->write = 1;
        if((random_low & 0x1F) <= 1) {

            /* sequential write.  */
            iosize = get_sequential_iosize(data, random_base>>32);
            request->size = iosize;
            if(random_low & 1) {
                /* forward */
                if(data->seq_pos3 + iosize > workload_generator->workload->worker->filesize)
                    data->seq_pos3 = 0;

                request->offset = data->seq_pos3;
                data->seq_pos3 += iosize;
            } else {
                /* backward */
                if(data->seq_pos4 < iosize)
                    data->seq_pos4 = workload_generator->workload->worker->filesize;

                data->seq_pos4 -= iosize;
                request->offset = data->seq_pos4;
            }
        } else {
            /* random write */
            iosize = get_random_iosize(data, random_base>>32);
            request->offset = random_base % (workload_generator->workload->worker->filesize / iosize);
            request->offset = request->offset  * iosize;
            request->size = iosize;
        }
	}

    return APR_SUCCESS;
}

static uint64_t mixed_request_generator_max_iosize(struct io_workload_generator *workload_generator)
{
    struct mixed_request_generator_data *data = (struct mixed_request_generator_data*) workload_generator->generator_data;

    return UINT64_C(2*1024*1024);
}

static uint64_t mixed_request_generator_weighted_iosize(struct io_workload_generator *workload_generator)
{
    return UINT64_C(4096);
}


static apr_status_t mixed_request_generator_reset(struct io_workload_generator *template_generator, struct io_workload *workload, uint64_t reqsize)
{
    int i=0;
    double random_factors[13];
    double sequential_factors[13];
    double acc_random_factors = 0.0;
    double acc_sequential_factors = 0.0;
    double current_random_factor;
    double current_sequential_factor;
    uint32_t sequential_value;
    uint32_t random_value;
    double base_random_value;
    double base_sequential_value;

    struct mixed_request_generator_data *data;
    if(workload->request_generator != NULL) {
        if(workload->request_generator->generator_data != NULL) {
            free(workload->request_generator->generator_data);
        }
        free(workload->request_generator);
        workload->request_generator = NULL;
    }
    workload->request_generator = malloc(sizeof(struct io_workload_generator));
    memcpy(workload->request_generator, template_generator, sizeof(struct io_workload_generator));
    workload->request_generator->generator_data = malloc(sizeof(struct mixed_request_generator_data));
    memcpy(workload->request_generator->generator_data, template_generator->generator_data, sizeof(struct mixed_request_generator_data));
    workload->request_generator->workload = workload;

    data = (struct mixed_request_generator_data*) workload->request_generator->generator_data;
    data->seq_pos1 = 0;
    data->seq_pos2 = 0;
    data->seq_pos3 = 0;
    data->seq_pos4 = 0;
    data->min_blocksize_idx = 0;
    while(blocksizes[data->min_blocksize_idx] < reqsize) {
        data->min_blocksize_idx = data->min_blocksize_idx + 1;
    }
    data->blocksizes = 13 - data->min_blocksize_idx;

    /**
     * Calculate request size probability:
     *      Random requests. Base reqsize is 4k. Request bandwidth halves on either side.
     *      Sequential requests. Base reqsize is 128k. Request bandwith halves on either side.
     *
     */
    for(i=data->min_blocksize_idx; i < data->min_blocksize_idx + data->blocksizes; ++i) {

        if(blocksizes[i] < 4096) {
            current_random_factor = (double) 4096/blocksizes[i];
        } else {
            current_random_factor = (double) blocksizes[i]/4096;
        }
        current_random_factor = current_random_factor * current_random_factor;
        current_random_factor = 1.0/current_random_factor;

        if(blocksizes[i] < 128*1024) {
            current_sequential_factor = (double) 128*1024/blocksizes[i];
        } else {
            current_sequential_factor = (double) blocksizes[i]/(128*1024);
        }
        current_sequential_factor = current_sequential_factor  * current_sequential_factor;
        current_sequential_factor = 1.0/current_sequential_factor;

        if(i>data->min_blocksize_idx) {
            current_random_factor += random_factors[i-1];
            current_sequential_factor += sequential_factors[i-1];
        }
        random_factors[i] = current_random_factor;
        sequential_factors[i] = current_sequential_factor;
    }

    for(i=data->min_blocksize_idx; i < data->min_blocksize_idx + data->blocksizes; ++i) {
        random_factors[i] = random_factors[i]/current_random_factor;
        sequential_factors[i] = sequential_factors[i]/current_sequential_factor;

        data->random_blocksize_prob[i] = UINT32_MAX*random_factors[i];
        data->sequential_blocksize_prob[i] = UINT32_MAX*sequential_factors[i];
    }

    return APR_SUCCESS;
}

apr_status_t mixed_request_generator_factory(
    struct io_workload_generator **request_generator,
    int write
)
{
    struct mixed_request_generator_data *data = calloc(1, sizeof(struct mixed_request_generator_data));
    *request_generator = malloc(sizeof(struct io_workload_generator));
    (*request_generator)->generator_data = data;
    (*request_generator)->fill_request = &mixed_request_generator_fill_request;
    (*request_generator)->max_io_size = &mixed_request_generator_max_iosize;
    (*request_generator)->weighted_io_size = &mixed_request_generator_weighted_iosize;
    (*request_generator)->reset = &mixed_request_generator_reset;

    return APR_SUCCESS;
}


