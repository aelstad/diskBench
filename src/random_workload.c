/*
  * random_workload.c
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

 struct random_request_generator_data {
    int write;
    uint64_t blocks;
    uint64_t req_size;
};

static apr_status_t random_request_generator_fill_request(
    struct io_workload_generator *workload_generator,
    struct io_request *request
)
{
    struct random_request_generator_data *data = (struct random_request_generator_data*) workload_generator->generator_data;
    uint64_t *random_seed = &workload_generator->workload->worker->random_seed;

	uint64_t random_base = random_uint64_t(random_seed);

	request->offset = random_base % data->blocks;
	request->offset = request->offset  * data->req_size;


    request->size = data->req_size;
    request->write = data->write;

    return APR_SUCCESS;
}

static uint64_t random_request_generator_max_iosize(struct io_workload_generator *workload_generator)
{
    struct random_request_generator_data *data = (struct random_request_generator_data*) workload_generator->generator_data;

    return data->req_size;
}

static uint64_t random_request_generator_weighted_iosize(struct io_workload_generator *workload_generator)
{
    return UINT64_C(4*1024);
}


static apr_status_t random_request_generator_reset(struct io_workload_generator *template_generator, struct io_workload *workload, uint64_t reqsize)
{
    if(workload->request_generator != NULL) {
        if(workload->request_generator->generator_data != NULL) {
            free(workload->request_generator->generator_data);
        }
        free(workload->request_generator);
        workload->request_generator = NULL;
    }
    ((struct random_request_generator_data*) template_generator->generator_data)->req_size = reqsize;
    workload->request_generator = malloc(sizeof(struct io_workload_generator));
    memcpy(workload->request_generator, template_generator, sizeof(struct io_workload_generator));
    workload->request_generator->generator_data = malloc(sizeof(struct random_request_generator_data));
    memcpy(workload->request_generator->generator_data, template_generator->generator_data, sizeof(struct random_request_generator_data));
    workload->request_generator->workload = workload;
    ((struct random_request_generator_data*) workload->request_generator->generator_data)->blocks = (workload->worker->filesize / reqsize);


    return APR_SUCCESS;
}

apr_status_t random_request_generator_factory(
    struct io_workload_generator **request_generator,
    int write
)
{
    struct random_request_generator_data *data = calloc(1, sizeof(struct random_request_generator_data));
    *request_generator = malloc(sizeof(struct io_workload_generator));
    data->write = write;
    (*request_generator)->generator_data = data;
    (*request_generator)->fill_request = &random_request_generator_fill_request;
    (*request_generator)->max_io_size = &random_request_generator_max_iosize;
    (*request_generator)->weighted_io_size = &random_request_generator_weighted_iosize;
    (*request_generator)->reset = &random_request_generator_reset;

    return APR_SUCCESS;
}


