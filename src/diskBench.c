/*
  * diskBench,c
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
#include "apr_getopt.h"

#define MAX_QUEUE_SIZE (4096)

#define REQUEST_FMT ("%3.1f %c")
#define THROUGHPUT_FMT ("%3.1f %cB/s")
#define BYTES_FMT ("%3.1f %cB")
#define IOPS_FMT ("%3.1f %cIOPS")


static void *APR_THREAD_FUNC ioworker(apr_thread_t *thd, void *data)
{
	struct io_workload *workload = (struct io_workload *) data;
	struct io_worker *worker = workload->worker;
	struct io_request *req;
	struct async_queue *queue;
	struct async_queue_entry *ioop;

	int events;
	apr_status_t rv;
	apr_time_t terminate_at;

    /* Generate IO-queue */
	rv = generic_queue_create(workload, workload->queue_depth, &queue);
	assert(rv == APR_SUCCESS);

    /* initalize to zero */
    workload->submitted_bytes = 0;
    workload->max_active = 0;

    workload->read_bytes = 0;
    workload->read_requests = 0;
    workload->read_elapsed = 0;
    workload->read_max_latency = 0;
    workload->read_min_latency = 0;

    workload->write_bytes = 0;
    workload->write_requests = 0;
    workload->write_elapsed = 0;
    workload->write_max_latency = 0;
    workload->write_min_latency = 0;

    workload->start_time = apr_time_now();
    terminate_at = workload->start_time + worker->options->max_execution_time;

	while(apr_time_now() <= terminate_at) {
        /* Fetch queue-entry */
        ioop = APR_RING_FIRST(queue->ready);
        APR_RING_REMOVE(ioop, link);

        req = &ioop->request;
        /* check if request will override IO-limit */
        if(workload->submitted_bytes + req->size > worker->iolimit) {
            APR_RING_INSERT_TAIL(queue->ready, ioop, async_queue_entry, link);
            break;
        }

        queue->free = queue->free - 1;
        queue->active = queue->active + 1;

        /* Call request-generator */
        rv = workload->request_generator->fill_request(workload->request_generator, req);
        assert(rv == APR_SUCCESS);

        /* submit io */
		req->pre_submission = apr_time_now();
		if(req->write) {
			rv = generic_queue_write(queue, ioop);
			assert(rv == APR_SUCCESS);
		} else {
			rv = generic_queue_read(queue, ioop);
			assert(rv == APR_SUCCESS);
		}
        workload->submitted_bytes += req->size;

		req->post_submission = apr_time_now();
		if(queue->active > workload->max_active) {
			workload->max_active = queue->active;
		}
		events = 0;
		rv = generic_queue_wait(queue, &events);
		assert(rv==APR_SUCCESS);
	}
	rv = generic_queue_barrier(queue);
	assert(rv==APR_SUCCESS);

	workload->end_time = apr_time_now();

	rv = generic_queue_destroy(queue);
    assert(rv==APR_SUCCESS);

	apr_thread_exit(thd, APR_SUCCESS);
	return NULL;
}

static uint64_t parse_size(const char *value)
{
    const char ord[] = "KMGT";
    const char *o = ord;

    char *end;
    uint64_t factor=K;
    uint64_t rv = apr_strtoi64(value, &end, 10);
    int i;

    for(i=0; i < strlen(ord); ++i) {
        if(*end == o[i]) {
            return rv*factor;
        }
        factor = factor * K;
    }
    return rv;
}

static char* print_size(apr_pool_t *pool, char *fmt, double value, int kvalue)
{
    const char ordbig[] = " KMGT";
    const char *o;
    o = ordbig;
    while(value > kvalue) {
        value = value / kvalue;
        ++o;
    }
    return apr_psprintf(pool, fmt, value, *o);
}

static char* print_time(apr_pool_t *pool, apr_time_t time)
{
    apr_time_exp_t exp;
    const char ordbig[] = "";

    apr_time_t one_usec = 1;
    apr_time_t one_msec = apr_time_from_msec(1);
    apr_time_t one_second = apr_time_from_sec(1);
    apr_time_t one_minute = apr_time_from_sec(60);
    apr_time_t one_hour = 60*one_minute;
    apr_time_t one_day = 24*one_hour;
    uint32_t days, hours, minutes, seconds, msecs=0, usecs=0;

    days = time / one_day;
    time = time - days*one_day;

    hours = time / one_hour;
    time = time - hours*one_hour;

    minutes = time / one_minute;
    time = time - minutes*one_minute;

    seconds = apr_time_sec(time);
    time = time - apr_time_from_sec(seconds);
    msecs = apr_time_msec(time);
    time = time - apr_time_from_msec(msecs);
    usecs = apr_time_usec(time);
    /*
    65d24h59m
    23h54m52s
    59m54s
    59.432s
    323.323ms
    323ms
    */
    if(days > 0) {
        return apr_psprintf(pool, "%dd%dh%dm", days, hours, minutes);
    } else if(hours > 0) {
        return apr_psprintf(pool, "%dh%dm%ds", hours, minutes, seconds);
    } else if(minutes > 0) {
        return apr_psprintf(pool, "%dm%d.%ds", minutes, seconds, msecs);
    } else if(seconds > 0) {
        return apr_psprintf(pool, "%d.%03ds", seconds, msecs);
    } else if(msecs > 0) {
        return apr_psprintf(pool, "%d.%03dms", msecs, usecs);
    } else {
        return apr_psprintf(pool, "%dus", usecs);
    }
}

static char* print_xml_start(apr_pool_t *pool)
{
    return apr_psprintf(pool, "<?xml version='1.0'?>\n");
}

static char* print_xml_tag_open(apr_pool_t *pool, char *xml_fragment, char *tagname)
{
    return apr_psprintf(pool, "%s<%s>\n", xml_fragment, tagname);
}

static char* print_xml_tag_close(apr_pool_t *pool, char *xml_fragment, char *tagname)
{
    return apr_psprintf(pool, "%s</%s>\n", xml_fragment, tagname);
}

static char* print_xml_tag_str(apr_pool_t *pool, char *xml_fragment, char *tagname, char *str)
{
    return apr_psprintf(pool, "%s<%s value=\"%s\">%s</%s>\n",xml_fragment, tagname, str, str, tagname);
}

static char* print_xml_tag_number(apr_pool_t *pool, char *xml_fragment, char *tagname, uint64_t number)
{
    return apr_psprintf(pool, "%s<%s value=\"%"APR_UINT64_T_FMT"\">%"APR_UINT64_T_FMT"</%s>\n",xml_fragment, tagname, number, number, tagname);
}


static char* print_xml_tag_size(apr_pool_t *pool, char *xml_fragment, char *tagname, char *fmt, uint64_t size)
{
    return apr_psprintf(pool, "%s<%s formatted=\"%s\" value=\"%"APR_UINT64_T_FMT"\">%"APR_UINT64_T_FMT"</%s>\n", xml_fragment,
                        tagname,
                        print_size(pool, fmt, size, K),
                        size,
                        size,
                        tagname);
}

static char* print_xml_tag_time(apr_pool_t *pool, char *xml_fragment, char *tagname, apr_time_t time)
{
    return apr_psprintf(pool, "%s<%s formatted=\"%s\" value=\"%"APR_UINT64_T_FMT"\">%"APR_UINT64_T_FMT"</%s>\n", xml_fragment, tagname,
                        print_time(pool, time),
                        (apr_uint64_t) time,
                        (apr_uint64_t) time,
                        tagname);
}






static apr_status_t create_worker(char *filename,
    struct io_worker_options *options,
    uint64_t bufsize,
    uint64_t filesize,
    uint64_t iolimit,
    struct io_worker **worker)
{
    apr_status_t rv;

    *worker = calloc(1, sizeof(struct io_worker));
    (*worker)->options = options;
    (*worker)->filename = filename;
    (*worker)->filesize = filesize;
    (*worker)->iolimit = iolimit;
    (*worker)->configured_iolimit = iolimit;
    (*worker)->random_seed = UINT64_C(88172645463325252);
    (*worker)->last_integrity_written_offset = 0;

    rv = platform_ops->create_io_buffer(&((*worker)->buf), bufsize);
    assert(rv == APR_SUCCESS);

    (*worker)->bufsize = bufsize;

    return rv;
}

static apr_status_t open_worker(struct io_worker *worker, double freespace_utilization)
{
    apr_status_t rv;

    rv = worker->options->platform_ops->file_open(worker->filename, &(worker->filesize),
                                                  freespace_utilization,
                                                  &(worker->truncate_file),
                                                  &(worker->file));
    assert(rv == APR_SUCCESS);

    return rv;

}

static apr_status_t destroy_workers(struct io_worker **workers, int count, apr_pool_t *pool)
{
	struct io_worker *worker;
	int i;
	apr_status_t rv;
	for(i=0; i < count; ++i) {
		worker = workers[i];

		rv = platform_ops->file_close(worker->file);
		assert(rv == APR_SUCCESS);
		if(worker->truncate_file && !worker->options->keep_files) {
            apr_file_remove(worker->filename, pool);
		}

		free(worker);
	}
	return APR_SUCCESS;
}

static apr_status_t print_statistics_seperator(apr_pool_t *pool)
{
    printf("-------------------------------------------------------------------------------------------------------------------------------------------------------\n");
}

static apr_status_t print_statistics_header(apr_pool_t *pool)
{
    printf("%-25s  %9s  %8s  %12s  %13s  %10s  %10s  %11s  %11s  %11s  %11s\n",
           "","Parallel","Avg IO","","","Bytes","Bytes","Time","Min","Avg","Max");
    printf("%-25s  %9s  %8s  %12s  %13s  %10s  %10s  %11s  %11s  %11s  %11s\n",
           "Workload","IOs","Size","Throughput","IOPS", "Written","Read","Elapsed","Latency","Latency","Latency");
    print_statistics_seperator(pool);
}

static apr_status_t dump_statistics(apr_pool_t *pool, struct io_worker_options *options,
    struct io_statistics *statistics, struct io_worker **workers,
    int count)
{
	struct io_workload *workload;
	struct io_statistics_line line;

	apr_time_t min;
	apr_time_t max;

	int i;
	int max_active=0;

	uint64_t weighted_iosize;
	uint64_t avg_iosize;
	uint64_t total_latency = 0;
	double bytes_per_second;
	double weight;

	line.read_elapsed = 0;
	line.write_elapsed = 0;
    line.bytes_read = 0;
    line.bytes_written = 0;
    line.read_requests = 0;
    line.write_requests = 0;
    line.weight = 0.0;
    line.weighted_bytes_per_second = 0.0;
    char *xml_fragment="";

    xml_fragment = print_xml_tag_open(pool, xml_fragment, "test_run");
    xml_fragment = print_xml_tag_open(pool, xml_fragment, "workloads");
	for(i=0; i < count; ++i) {
		workload = workers[i]->workload;
		if(workload == NULL)
            continue;
        xml_fragment = print_xml_tag_open(pool, xml_fragment, "workload");
        xml_fragment = print_xml_tag_number(pool, xml_fragment, "worker", i);
        xml_fragment = print_xml_tag_number(pool, xml_fragment, "depth", workload->max_active);
        xml_fragment = print_xml_tag_size(pool, xml_fragment, "read_requests", REQUEST_FMT, workload->read_requests);
        xml_fragment = print_xml_tag_size(pool, xml_fragment, "write_requests", REQUEST_FMT, workload->write_requests);
        xml_fragment = print_xml_tag_size(pool, xml_fragment, "bytes_written", BYTES_FMT, workload->write_bytes);
        xml_fragment = print_xml_tag_size(pool, xml_fragment, "bytes_read", BYTES_FMT, workload->read_bytes);
        xml_fragment = print_xml_tag_time(pool, xml_fragment, "wait_time_write", workload->write_elapsed);
        xml_fragment = print_xml_tag_time(pool, xml_fragment, "wait_time_read", workload->read_elapsed);
        xml_fragment = print_xml_tag_time(pool, xml_fragment, "min_write_latency", workload->write_min_latency);
        xml_fragment = print_xml_tag_time(pool, xml_fragment, "max_write_latency", workload->write_max_latency);
        xml_fragment = print_xml_tag_time(pool, xml_fragment, "min_read_latency", workload->read_min_latency);
        xml_fragment = print_xml_tag_time(pool, xml_fragment, "max_read_latency", workload->read_max_latency);


        total_latency += (workload->read_elapsed + workload->write_elapsed);
		weighted_iosize = workload->request_generator->weighted_io_size(workload->request_generator);
		max_active += workload->max_active;
		line.read_requests += workload->read_requests;
		line.write_requests += workload->write_requests;

		line.bytes_read +=  workload->read_bytes;
		line.bytes_written += workload->write_bytes;
		line.read_elapsed += workload->read_elapsed;
		line.write_elapsed += workload->write_elapsed;

		if(i==0) {
            min = workload->start_time;
            max = workload->end_time;
            line.min_read_latency = workload->read_min_latency;
            line.max_read_latency = workload->read_max_latency;
            line.min_write_latency = workload->write_min_latency;
            line.max_write_latency = workload->write_max_latency;
		} else {
		    min = min_time(min, workload->start_time);
		    max = max_time(max, workload->end_time);
		    line.min_read_latency = min_time(line.min_read_latency, workload->read_min_latency);
		    line.max_read_latency = max_time(line.max_read_latency, workload->read_max_latency);

		    line.min_write_latency = min_time(line.min_write_latency, workload->write_min_latency);
		    line.max_write_latency = max_time(line.max_write_latency, workload->write_max_latency);
		}

        avg_iosize = (workload->read_bytes + workload->write_bytes)/(workload->read_requests + workload->write_requests);
        bytes_per_second =
            (((double) workload->read_bytes + workload->write_bytes)/(double) (workload->end_time-workload->start_time))
            *apr_time_from_sec(1);


        if(avg_iosize < weighted_iosize)
            weight = weighted_iosize/avg_iosize;
        else
            weight = avg_iosize/weighted_iosize;

        weight = 10.0/(weight + workload->queue_depth);

        line.weighted_bytes_per_second += weight *bytes_per_second;
        line.weight += weight / count;

        xml_fragment = print_xml_tag_close(pool, xml_fragment, "workload");
	}
    if(line.read_requests == 0 && line.write_requests == 0)
        return;

	if(statistics->lines->nelts == 0 || max_active > statistics->max_active)
	{
        statistics->max_active = max_active;
	}

    if(line.read_requests == 0) {
        line.min_latency = line.min_write_latency;
        line.max_latency = line.max_write_latency;
    } else if(line.write_requests ==  0) {
        line.min_latency = line.min_read_latency;
        line.max_latency = line.max_read_latency;
    } else {
        line.min_latency = min_time(line.min_read_latency, line.min_write_latency);
        line.max_latency = max_time(line.max_read_latency, line.max_write_latency);
    }
	line.total_elapsed= max-min;
	line.total_bytes = line.bytes_read + line.bytes_written;
	line.total_requests = line.read_requests + line.write_requests;
	line.avg_latency = ((double) total_latency) / ((double)line.total_requests);
	line.bytes_per_second = ((double)line.total_bytes/(double)line.total_elapsed)*apr_time_from_sec(1);
	line.bytes_per_io =(double)line.total_bytes/(double) line.total_requests;

    if(statistics->lines->nelts == 0) {
        statistics->bytes_read = line.bytes_read;
        statistics->bytes_written = line.bytes_written;
        statistics->accumulated_weight = line.weight;
        statistics->accumulated_weighted_bytes_per_second = line.weighted_bytes_per_second;
        statistics->read_requests = line.read_requests;
        statistics->write_requests = line.write_requests;
        statistics->elapsed = line.total_elapsed;
        statistics->min_throughput = line.bytes_per_second;
        statistics->max_throughput = line.bytes_per_second;
    } else {
        statistics->bytes_read += line.bytes_read;
        statistics->bytes_written += line.bytes_written;
        statistics->accumulated_weight += line.weight;
        statistics->accumulated_weighted_bytes_per_second += line.weighted_bytes_per_second;
        statistics->read_requests += line.read_requests;
        statistics->write_requests += line.write_requests;
        statistics->elapsed += line.total_elapsed;
        if(line.bytes_per_second < statistics->min_throughput)
        {
            statistics->min_throughput = line.bytes_per_second;
        }
        if(line.bytes_per_second > statistics->max_throughput) {
            statistics->max_throughput = line.bytes_per_second;
        }
    }
	APR_ARRAY_PUSH(statistics->lines, struct io_statistics_line)=line;

	xml_fragment = print_xml_tag_close(pool, xml_fragment, "workloads");

    double iops = (((double) line.total_requests)/(double) (line.total_elapsed))*apr_time_from_sec(1);
	printf("%-25s  %9d  %8s  %12s  %13s  %10s  %10s  %11s  %11s  %11s  %11s\n",
	statistics->description,
	max_active,
	print_size(pool, BYTES_FMT, line.bytes_per_io, K),
	print_size(pool, THROUGHPUT_FMT, line.bytes_per_second, K),
    print_size(pool, IOPS_FMT, iops, K),
    print_size(pool, BYTES_FMT, (double) line.bytes_written, K),
    print_size(pool, BYTES_FMT, (double) line.bytes_read, K),
    print_time(pool, line.total_elapsed),
    print_time(pool, line.min_latency),
    print_time(pool, line.avg_latency),
    print_time(pool, line.max_latency));
    xml_fragment = print_xml_tag_str(pool, xml_fragment, "description", statistics->description);
    xml_fragment = print_xml_tag_number(pool, xml_fragment, "concurrent_iops", max_active);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "bytes_per_io", BYTES_FMT, line.bytes_per_io);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "bytes_per_second", THROUGHPUT_FMT, line.bytes_per_second);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "iops", IOPS_FMT, iops);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "write_requests", REQUEST_FMT, line.write_requests);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "read_requests", REQUEST_FMT, line.read_requests);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "bytes_written", BYTES_FMT, line.bytes_written);
    xml_fragment = print_xml_tag_size(pool, xml_fragment, "bytes_read", BYTES_FMT, line.bytes_read);
    xml_fragment = print_xml_tag_time(pool, xml_fragment, "time_elapsed", line.total_elapsed);
    xml_fragment = print_xml_tag_time(pool, xml_fragment, "min_latency", line.min_latency);
    xml_fragment = print_xml_tag_time(pool, xml_fragment, "avg_latency", line.avg_latency);
    xml_fragment = print_xml_tag_time(pool, xml_fragment, "max_latency", line.max_latency);
    xml_fragment = print_xml_tag_close(pool, xml_fragment, "test_run");
    options->xml_output = apr_pstrcat(options->pool, options->xml_output, xml_fragment, NULL);

	return APR_SUCCESS;
}

/* Run tests over a queue-depth test */
static apr_status_t run_tests(
    char *description,
    struct io_worker_options *options,
    struct io_worker **worker,
    int worker_count,
    int auto_terminate_request,
    int auto_terminate_depth,
    uint64_t *max_reqsize,
    uint64_t separate_statistics_reqsize,
    char *separate_statistics_description)
{

 	apr_thread_t **threads = malloc(sizeof(apr_thread_t*)*worker_count);
 	apr_thread_t *thread;
    struct io_statistics *statistics;
    struct io_statistics *separate_statistics;
    struct io_statistics *current_statistics;
 	apr_pool_t *pool;
 	apr_pool_t *local;
 	apr_status_t rv;
 	int i, depth, depthidx, reqsizeidx, gen_separate_statistics;


    statistics = NULL;
    separate_statistics = NULL;
    current_statistics = NULL;

    /* Keep a moving average to terminate */
 	#define MIN_TESTS 3
 	double reqsize_min_throughput[MIN_TESTS];
 	double reqsize_max_throughput[MIN_TESTS];
    double depth_throughput[MIN_TESTS];
    pool = options->pool;
    apr_pool_create(&local, pool);

    int terminate_reqsize = 0;
    for(i=0; i < MIN_TESTS; ++i) {
        reqsize_min_throughput[i] = 0.0;
        reqsize_max_throughput[i] = 0.0;
    }
    for(reqsizeidx=0; !terminate_reqsize; ++reqsizeidx) {
        int terminate_depth=0;
        double min_depth_throughput;
        double max_depth_throughput;
        for(i=0; i < MIN_TESTS; ++i) {
            depth_throughput[i] = 0.0;
        }
        int has_workload = 0;
        for(depthidx=0; !terminate_depth; ++depthidx) {
            /* validate that buffer is big enough */
            gen_separate_statistics = 1;
            for(i=0; i< worker_count; ++i) {
                struct io_workload *workload = worker[i]->workload;
                if(workload == NULL)
                    continue;

                has_workload = 1;

                if(depthidx >= workload->depths->nelts)  {
                    terminate_depth = 1;
                }

                if(reqsizeidx >= workload->reqsizes->nelts)  {
                    terminate_reqsize = 1;
                }
                if(terminate_depth || terminate_reqsize) {
                    break;
                }

                int depth = APR_ARRAY_IDX(workload->depths, depthidx, uint32_t);
                uint64_t reqsize = APR_ARRAY_IDX(workload->reqsizes, reqsizeidx, uint64_t);
                gen_separate_statistics = gen_separate_statistics && reqsize == separate_statistics_reqsize
                    && separate_statistics_description != NULL;

                workload->worker = worker[i];
                workload->queue_depth = depth;
                workload->template_generator->reset(
                    workload->template_generator, workload, reqsize);
                if(max_reqsize != NULL && reqsize > *max_reqsize) {
                    *max_reqsize = reqsize;
                }

                uint64_t bufsize = worker[i]->bufsize / depth;
                bufsize = bufsize - bufsize % worker[i]->options->platform_ops->get_page_size();
                if(workload->request_generator->max_io_size(workload->request_generator) > bufsize) {
                    terminate_depth = 1;
                }
            }
            if(!has_workload) {
                terminate_depth = 1;
                terminate_reqsize = 1;
            }
            if(terminate_depth || terminate_reqsize)
                continue;

            if(gen_separate_statistics && separate_statistics == NULL) {
                separate_statistics = apr_pcalloc(pool, sizeof(struct io_statistics));
                separate_statistics->lines = apr_array_make(pool, 0, sizeof(struct io_statistics_line));
                separate_statistics->description = apr_pstrdup(pool, separate_statistics_description);
            } else if(!gen_separate_statistics && statistics == NULL) {
                statistics = apr_pcalloc(pool, sizeof(struct io_statistics));
                statistics->lines = apr_array_make(pool, 0, sizeof(struct io_statistics_line));
                statistics->description = apr_pstrdup(pool, description);
            }

            current_statistics = gen_separate_statistics ? separate_statistics : statistics;

            /* Start threads */
            for(i=0; i< worker_count; ++i) {
                if(worker[i]->workload == NULL)
                    continue;
                rv = apr_thread_create(&(threads[i]), NULL, ioworker, worker[i]->workload, local);
                assert(rv == APR_SUCCESS);
            }
            /* Wait for threads */
            for(i=0; i<worker_count; ++i) {
                if(worker[i]->workload == NULL)
                    continue;

                thread = threads[i];
                rv = apr_thread_join(&rv, thread);
                assert(rv==APR_SUCCESS);

                rv = worker[i]->options->platform_ops->file_flush(worker[i]->file);
                assert(rv==APR_SUCCESS);
            }

            assert(rv == APR_SUCCESS);
            /* Dump statistics */
            dump_statistics(local, options, current_statistics, worker, worker_count);

            double throughput = APR_ARRAY_IDX(current_statistics->lines,current_statistics->lines->nelts-1, struct io_statistics_line).bytes_per_second;
            depth_throughput[depthidx % MIN_TESTS] = throughput;

            if(depthidx == 0) {
                min_depth_throughput = throughput;
                max_depth_throughput = throughput;
            } else {
                if(throughput < min_depth_throughput)
                    min_depth_throughput = throughput;
                if(throughput > max_depth_throughput)
                    max_depth_throughput = throughput;
            }

            if(depthidx >= MIN_TESTS) {
                double avg_throughput = 0.0;
                for(i=0; i < MIN_TESTS; ++i) {
                    avg_throughput += depth_throughput[i];
                }
                avg_throughput /= MIN_TESTS;
                if(throughput <= avg_throughput) {
                    terminate_depth = terminate_depth || auto_terminate_depth;
                }
            }
        }
        reqsize_min_throughput[reqsizeidx % MIN_TESTS] = min_depth_throughput;
        reqsize_max_throughput[reqsizeidx % MIN_TESTS] = max_depth_throughput;
        if(reqsizeidx >= MIN_TESTS) {
                double avg_min_throughput = 0.0;
                double avg_max_throughput = 0.0;
                for(i=0; i < MIN_TESTS; ++i) {
                    avg_min_throughput += reqsize_min_throughput[i];
                    avg_max_throughput += reqsize_max_throughput[i];
                }
                avg_min_throughput /= MIN_TESTS;
                avg_max_throughput /= MIN_TESTS;
                if(min_depth_throughput <= avg_min_throughput && max_depth_throughput <= avg_max_throughput) {
                    terminate_reqsize = terminate_reqsize || auto_terminate_request;
                }

        }

        apr_pool_clear(local);
    }

    if(options->statistics_array != NULL) {
        if(separate_statistics != NULL) {
            APR_ARRAY_PUSH(options->statistics_array, struct io_statistics*) = separate_statistics;
        }
        if(statistics != NULL) {
            APR_ARRAY_PUSH(options->statistics_array, struct io_statistics*) = statistics;
        }
    }


    apr_pool_destroy(local);

    free(threads);
}

static void show_help(apr_getopt_option_t const *options)
{
	printf("diskBench accepts the following arguments:\n\n");
	while(options->name != NULL) {
		printf("\t%s\n",options->description);
		++options;
	}
	printf("\nExamples:\n");
    printf("\tdiskBench                                        \tRun test using default settings. Maximum 8GB of disk space needed.\n");
	printf("\tdiskBench -c 0                                   \tUse default settings and run a quick test.\n");
	printf("\tdiskBench -t 180 -s 4K  -f /dev/sdb \tRun a thorough test against /dev/sdb\n");
}

static void prepare_workload(struct io_worker *worker, struct io_workload_generator *generator,
    apr_array_header_t *reqsize_array, apr_array_header_t *depth_array)
{
    if(generator == NULL) {
        if(worker->workload != NULL) {
            free(worker->workload);
        }
        worker->workload = NULL;
    } else {
        if(worker->workload == NULL) {
            worker->workload = calloc(1, sizeof(struct io_workload));
        }
        worker->workload->template_generator = generator;
        worker->workload->depths = depth_array;
        worker->workload->reqsizes = reqsize_array;
    }
}

static char * print_array_size(apr_pool_t *pool, uint64_t max_size, apr_array_header_t *reqsizes)
{
    int i=0;
    char *rv="";
    for(i=0; i < reqsizes->nelts; ++i)  {
        uint64_t size = APR_ARRAY_IDX(reqsizes, i, uint64_t);
        if(size > max_size)
            continue;
        if(strlen(rv)==0) {
            rv = print_size(pool, "%3.0f%cB", size, K);
        } else {
            rv = apr_psprintf(pool, "%s, %s", rv, print_size(pool, "%3.0f%cB", size, K));
        }
    }
    return rv;
}

static char * print_array_depths(apr_pool_t *pool, uint32_t max_depth, apr_array_header_t *queue_depths)
{
    int i=0;
    char *rv="";
    for(i=0; i < queue_depths->nelts; ++i)  {
        uint32_t depth = APR_ARRAY_IDX(queue_depths, i, uint32_t);
        if(depth > max_depth)
            continue;
        if(strlen(rv)==0) {
            rv = apr_psprintf(pool, "%d", APR_ARRAY_IDX(queue_depths, i, uint32_t));
        } else {
            rv = apr_psprintf(pool, "%s, %d", rv, depth);
        }
    }
    return rv;
}

int main(int argc, const char * const * argv)
{	
    struct io_worker_options options;
	struct io_worker **workers;
	struct io_worker *worker;

	apr_pool_t *pool;
	apr_status_t rv;

    apr_getopt_t *opt;
    int i,optch;
	const char *optarg;
	char *last, *last2;
	char *filetoken;
	char *filename;
	char *str_filesize;
	char *str_iobufsize;
	char *str_iolimit;
	char *machineId = "Unkown";
	int quick =0;
	int auto_terminate_request = 1;
	int auto_terminate_depth = 1;
	uint64_t iobufsize = UINT64_C(32*1024*1024);
	uint64_t max_requestsize_random = 0;
	uint64_t max_requestsize_sequential = 0;

	struct io_workload_generator *workload;
	struct io_statistics *statistics;
    apr_array_header_t *worker_array;
    apr_array_header_t *queue_depth_array;
    apr_array_header_t *queue_depth_array_create;
    apr_array_header_t *requestsize_array_create;
    apr_array_header_t *requestsize_array_random;
    apr_array_header_t *requestsize_array_sequential;
    apr_file_t *xml_file = NULL;

    uint32_t sector_size = 512;

	static const apr_getopt_option_t opt_option[] = {
	        /* long-option, short-option, has-arg flag, description */
	        { "machineId", 'm', TRUE, "[-m,--machineId=MachineID]\n\t\tOutput identification. Default is Unknown." },
	        { "bufsize", 'b', TRUE, "[-b,--bufsize=<size>]\n\t\tIOBuffersize per worker. Limits concurrent IO and defaults to 32MB. Must be specified before files."},
	        { "files", 'f', TRUE, "[-f,--files=<file>;<filesize>;<iolimit>,[<file>;<filesize>;<iolimit>]\n\t\tFilenames used during testing. You may test several files at the same time each in a seperate thread."\
	        " \n\t\tFilesize, iolimit can be omitted or set to 0 to use resonable default values\n\t\t(max(existing size, 80% of freespace) and no-limit). New files are created/written (see -p) and then truncated. "
	        "\n\t\t\t-f diskBench.dat                \tThis is the default. Creates a file using maximum 80% of freespace."\
	        "\n\t\t\t-f /dev/sda                     \tTest/Overwrite umounted linux volume. Run test as root. "\
	        "\n\t\t\t-f \\\\.\\E:                        \tTest/Overwrite umounted windows volume. Run test as Administrator. "\
            "\n\t\t\t-f a.dat;8GB -f b.dat;8GB       \tCreate and test two files of 8GB."\
            "\n\n"
	        },
	        { "validateExisting", 'v', FALSE, "[-v,--validateExisting\n\t\tValidate integrity of existing files. Useful to test power-loss protection.\n\t\tFiles/devices bus have been written previously or this will fail." },
	        { "preparationTime", 'p', TRUE, "[-p <time_in_seconds', --preparationTime=<time_in_seconds>]\n\t\tMax preparation time before tests in seconds. Default is 300."},
            { "time", 't', TRUE, "[-t <time_in_seconds>,--time=<time_in_seconds>]\n\t\tExecution time per test in seconds. Default is 30." },
	        { "randomData", 'd', TRUE, "[-d,--randomData=0|1]\n\t\tTurn pseudorandom writes on (1) or off (0). Random data is on by default.\n\t\tSSDs with Sandforce controllers perform even better with repeating/nonrandom data." },
            { "queueDepth", 'q', TRUE, "[-q,--queueDepth=<qd1>[,<qd2>..]\n\t\tSpecifies which s to test.\n\t\tDefaults to 1,2,4,.. until performance no longer increases." },
            { "requestSize", 'r', TRUE, "[-r,--requestSize=<size0>[,<size1>..]\n\t\tSpecifies which requestsizes to test.\n\t\tDefaults to sectorSize,2*sectorSize,4*sectorSize,...until performance no longer increases." },
            { "sectorSize", 's', TRUE, "[-s,--sectorSize=<size>\n\t\tSpecifies minimum IO size. Defaults to 512, but some hardware/OS may require 4096." },
            { "complete", 'c', TRUE, "[-c,--complete=0|1]\n\t\tRun a short (default) or complete test. A short test limits sequential read/write to 128K and random read/write to 4K."},
            { "xmlOutput", 'x', TRUE, "[-x,--xmlOutput=<filename>\n\t\tWrite test results to xml-file. " },
            { "keepFiles", 'k', FALSE, "[-k,--keepFiles\n\t\tDon't delete created files. " },
	        { "help", 'h', FALSE, "[-h --showHelp]\n\t\tShow help" },
	        { NULL, 0, 0, NULL }, /* end (a.k.a. sentinel) */
	};

	setvbuf(stdout, 0, _IONBF, 0);
	apr_app_initialize(&argc, &argv, NULL);
	apr_time_t start_time = apr_time_now();
	apr_pool_create(&pool, NULL);
	

   /* initialize apr_getopt_t */
    apr_getopt_init(&opt, pool, argc, argv);
    
	printf(VERSION "\n\n\tFor usage instructions execute diskBench -h.\n\n");

	options.platform_ops = platform_ops;
	options.validate_existing = 0;
	options.write_random = 1;
	options.max_execution_time = apr_time_from_sec(30);
	options.max_preparation_time = apr_time_from_sec(300);
    options.pool = pool;
    options.xml_output = print_xml_start(pool);
    options.xml_output = print_xml_tag_open(pool, options.xml_output, "diskBench");
    options.keep_files = 0;

    quick = 1;



	worker_array = apr_array_make(pool, 0, sizeof(struct io_worker*));
	queue_depth_array = apr_array_make(pool, 0, sizeof(uint32_t));
	queue_depth_array_create = apr_array_make(pool, 0, sizeof(uint32_t));
	requestsize_array_create = apr_array_make(pool, 0, sizeof(uint64_t));
	requestsize_array_random = apr_array_make(pool, 0, sizeof(uint64_t));
	requestsize_array_sequential = apr_array_make(pool, 0, sizeof(uint64_t));
	options.statistics_array = apr_array_make(pool, 0, sizeof(struct io_statistics*));

    /* parse the all options based on opt_option[] */
    while ((rv = apr_getopt_long(opt, opt_option, &optch, &optarg)) == APR_SUCCESS) {
        switch (optch) {
        case 'b':
            iobufsize = parse_size(optarg);
            break;
        case 'f':
            filetoken = apr_strtok(apr_pstrdup(pool,optarg),"&",&last);
            while(filetoken != NULL) {
                filename = apr_strtok(filetoken, ";", &last2);
                str_filesize = apr_strtok(NULL,";",&last2);
                str_iolimit = apr_strtok(NULL,";",&last2);
                if(filename == NULL || *filename == '\0') {
                    printf("Error parsing file argment %s", filetoken);
                    return 1;
                }
                rv = create_worker(filename, &options,
                    iobufsize,
                    str_filesize != NULL && *str_filesize != '\0' ? parse_size(str_filesize) : 0,
                    str_iolimit != NULL && *str_iolimit != '\0' && parse_size(str_iolimit)>0 ? parse_size(str_iolimit) : UINT64_MAX,
                    &worker
                    );
                if(rv != APR_SUCCESS) {
                    printf("Error creating file %s",filename);
                    return 1;
                }
                APR_ARRAY_PUSH(worker_array, struct io_worker*) = worker;
                filetoken = apr_strtok(NULL,"&",&last);
            }
            break;
        case 'd':
			options.write_random = atoi(optarg);
			break;
        case 's':
            sector_size = parse_size(optarg);
			break;
        case 'q':
            last = apr_strtok(apr_pstrdup(pool,optarg), ",", &last2);
            while(last != NULL) {
                APR_ARRAY_PUSH(queue_depth_array, uint32_t) = parse_size(last);
                last = apr_strtok(NULL, ",", &last2);
            }
            auto_terminate_depth = 0;
			break;
        case 'r':
            last = apr_strtok(apr_pstrdup(pool,optarg), ",", &last2);
            while(last != NULL) {
                APR_ARRAY_PUSH(requestsize_array_random, uint64_t) = parse_size(last);
                APR_ARRAY_PUSH(requestsize_array_sequential, uint64_t) = parse_size(last);
                last = apr_strtok(NULL, ",", &last2);
            }
            auto_terminate_request = 0;
			break;
        case 't':
			options.max_execution_time = apr_time_from_sec(apr_atoi64(optarg));
			break;
        case 'p':
            options.max_preparation_time = apr_time_from_sec(apr_atoi64(optarg));
            break;
        case 'm':
			machineId =optarg;
			break;
        case 'c':
            quick = !(atoi(optarg)==1);
            break;
        case 'v':
            options.validate_existing = 1;
            break;
        case 'x':
            if(apr_file_open(&xml_file, optarg, APR_WRITE|APR_CREATE|APR_TRUNCATE,0, pool) != APR_SUCCESS) {
                printf("Could not open file for xmloutput");
                exit(1);
            }
            break;
        case 'k':
            options.keep_files = 1;
            break;
        case 'h':
        	show_help(opt_option);
        	return 1;
            break;

        }
    }

    if(worker_array->nelts == 0) {
        rv = create_worker("diskBench.dat", &options,
                    UINT64_C(32*1024*1024),
                    0,
                    APR_UINT64_MAX,
                    &worker
                    );
        if(rv != APR_SUCCESS) {
            printf("Error creating default worker\n");
            return 1;
        }
        APR_ARRAY_PUSH(worker_array, struct io_worker*) = worker;
    }


    if(rv != APR_SUCCESS && rv != APR_EOF) {
    	show_help(opt_option);
    	return 1;
    }

    workers = calloc(worker_array->nelts, sizeof(struct io_worker*));

    options.xml_output = print_xml_tag_open(pool, options.xml_output, "prepare_and_validate");

    printf("\n");
    printf("Preparing files");
	printf("\n\n");


    print_statistics_header(pool);
    apr_array_clear(queue_depth_array_create);
    APR_ARRAY_PUSH(queue_depth_array_create, uint32_t) = 2;

    apr_array_clear(requestsize_array_create);
    uint64_t reqsize_create = iobufsize/2;
    while(reqsize_create > sector_size && reqsize_create*worker_array->nelts > iobufsize) {
        reqsize_create = reqsize_create/2;
    }
    APR_ARRAY_PUSH(requestsize_array_create, uint64_t) = reqsize_create;
    apr_time_t max_execution_time = options.max_execution_time;

	for(i=0; i < worker_array->nelts; ++i) {

	    workers[i] =  APR_ARRAY_IDX(worker_array,i, struct io_worker*);
	    if(i > 0 && workers[i]->filesize == 0) {
	        workers[i]->filesize = workers[0]->filesize;
	    }
        open_worker(workers[i], 0.8/worker_array->nelts);
	    workers[i]->description =
            apr_psprintf(pool,"%s;%s;%s",workers[i]->filename,
                print_size(pool, "%.0f%cB", workers[i]->filesize, 1024),
                print_size(pool, "%.0f%cB", workers[i]->bufsize, 1024));


        workers[i]->options->max_execution_time = workers[i]->options->max_preparation_time;

        if(workers[i]->truncate_file) {
            rv = sequential_request_generator_factory(&workload, 1);
            prepare_workload(workers[i], workload, requestsize_array_create, queue_depth_array_create);
            workers[i]->iolimit = workers[i]->filesize;
        } else {
            if(options.validate_existing) {
                rv = sequential_request_generator_factory(&workload, 0);
                workers[i]->iolimit = workers[i]->filesize;
                prepare_workload(workers[i], workload, requestsize_array_create, queue_depth_array_create);
            } else {
                prepare_workload(workers[i], NULL, NULL, NULL);
            }
        }
    }
    run_tests("Creating/Validating files", &options, workers,
              worker_array->nelts, auto_terminate_request, auto_terminate_depth, NULL, 0, NULL);
    for(i=0; i < worker_array->nelts; ++i) {
        workers[i]->options->max_execution_time = max_execution_time;
        if(workers[i]->truncate_file) {
            workers[i]->filesize = workers[i]->workload->write_bytes;
            workers[i]->iolimit = workers[i]->configured_iolimit;
            workers[i]->options->platform_ops->file_truncate(workers[i]->file, &(workers[i]->filesize));
        }
    }

    print_statistics_seperator(pool);

    options.xml_output = print_xml_tag_close(pool, options.xml_output, "prepare_and_validate");

    options.xml_output = print_xml_tag_open(pool, options.xml_output, "tests");

    printf("\n");
    printf("Running tests");
	printf("\n");

	if(queue_depth_array->nelts == 0) {
	    i=1;
        do {
            APR_ARRAY_PUSH(queue_depth_array, uint32_t) = i;
            i=i*2;
        } while(i <= MAX_QUEUE_SIZE);
	}
    if(requestsize_array_random->nelts == 0 && requestsize_array_sequential->nelts ==0) {
        if(quick) {
            APR_ARRAY_PUSH(requestsize_array_random, uint64_t) = 4*1024;
            APR_ARRAY_PUSH(requestsize_array_sequential, uint64_t) = 128*1024;
        }
        else {
            i=sector_size;
            while(i <= iobufsize/worker_array->nelts) {
                APR_ARRAY_PUSH(requestsize_array_random, uint64_t) = i;
                APR_ARRAY_PUSH(requestsize_array_sequential, uint64_t)  =  i;
                i = i*2;
            }
        }
        }
    print_statistics_header(pool);

    rv = sequential_request_generator_factory(&workload, 1);
    assert(rv == APR_SUCCESS);
    for(i=0; i < worker_array->nelts; ++i) {
        prepare_workload(workers[i], workload, requestsize_array_sequential, queue_depth_array);
    }
    run_tests("Sequential write", &options, workers, worker_array->nelts,
               auto_terminate_request,
               auto_terminate_depth,
               &max_requestsize_sequential,
               128*1024, "Sequential write 128k");

    rv = sequential_request_generator_factory(&workload, 0);
    assert(rv == APR_SUCCESS);
    for(i=0; i < worker_array->nelts; ++i) {
        prepare_workload(workers[i], workload, requestsize_array_sequential, queue_depth_array);
    }
    run_tests("Sequential read", &options, workers, worker_array->nelts,
               auto_terminate_request,
               auto_terminate_depth,
               &max_requestsize_sequential,
                128*1024, "Sequential read 128k");

    rv = random_request_generator_factory(&workload, 1);
    assert(rv == APR_SUCCESS);
    for(i=0; i < worker_array->nelts; ++i) {
        prepare_workload(workers[i], workload, requestsize_array_random, queue_depth_array);
    }
    run_tests("Random write", &options, workers, worker_array->nelts,
               auto_terminate_request,
               auto_terminate_depth,
               &max_requestsize_random,
               4096, "Random write 4k");

    rv = random_request_generator_factory(&workload, 0);
    assert(rv == APR_SUCCESS);
    for(i=0; i < worker_array->nelts; ++i) {
        prepare_workload(workers[i], workload, requestsize_array_random, queue_depth_array);
    }
    run_tests("Random read", &options, workers, worker_array->nelts,
               auto_terminate_request,
               auto_terminate_depth,
               &max_requestsize_random,
                4096, "Random read 4k");

	char *sequential_requestsizes = print_array_size(pool, max_requestsize_sequential, requestsize_array_sequential);
	char *random_requestsizes = print_array_size(pool, max_requestsize_random, requestsize_array_random);

    rv = mixed_request_generator_factory(&workload, 1);
    apr_array_clear(requestsize_array_random);
    APR_ARRAY_PUSH(requestsize_array_random, uint64_t) = (uint64_t) sector_size;
    for(i=0; i < worker_array->nelts; ++i) {
        prepare_workload(workers[i], workload, requestsize_array_random, queue_depth_array);
    }
    run_tests("Totaliaris mix", &options, workers, worker_array->nelts,
               auto_terminate_request,
               auto_terminate_depth,
               NULL,
               0, NULL);

    options.xml_output = print_xml_tag_close(pool, options.xml_output, "tests");
    apr_time_t end_time = apr_time_now();

    print_statistics_seperator(pool);


    options.xml_output = print_xml_tag_open(pool, options.xml_output, "summary");

    printf("\nSummary:\n\n");
    printf("%-26s %s\n", "diskBench version: ", VERSION);
    uint32_t max_active = 0;
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
    uint64_t total_requests = 0;
    uint64_t read_requests = 0;
    uint64_t write_requests = 0;
    double overall_score;
    for(i=0; i < options.statistics_array->nelts; ++i) {
        statistics = APR_ARRAY_IDX(options.statistics_array, i, struct io_statistics*);
        bytes_read += statistics->bytes_read;
        bytes_written += statistics->bytes_written;
        total_requests += statistics->read_requests + statistics->write_requests;
        write_requests += statistics->write_requests;
        read_requests += statistics->read_requests;
        if(statistics->max_active >  max_active)
            max_active = statistics->max_active;
        overall_score = statistics->accumulated_weighted_bytes_per_second/statistics->accumulated_weight;
    }
    char *depths = print_array_depths(pool, max_active/worker_array->nelts, queue_depth_array);

    printf("%-26s %s\n", "Configuration description:", machineId);
    printf("%-26s %s\n", "Preparation time:", print_time(pool, options.max_preparation_time));
    printf("%-26s %s\n", "Time per test:", print_time(pool, options.max_execution_time));
    printf("%-26s %d\n", "Random writing: ", options.write_random);
    printf("%-26s %s\n", "Iobuffer size: ", print_size(pool, "%.0f%cB", iobufsize, K));
    printf("%-26s %s\n", "Iosize(s) sequential:", sequential_requestsizes);
    printf("%-26s %s\n", "Iosize(s) random:", random_requestsizes);
    printf("%-26s %s\n", "Queue depths (per worker):", depths);

    options.xml_output = print_xml_tag_str(pool,options.xml_output, "configuration_description", machineId);
    options.xml_output = print_xml_tag_time(pool,options.xml_output, "preparation_time", options.max_preparation_time);
    options.xml_output = print_xml_tag_time(pool,options.xml_output, "time_per_test", options.max_execution_time);
    options.xml_output = print_xml_tag_number(pool,options.xml_output, "random_writing", options.write_random);
    options.xml_output = print_xml_tag_size(pool,options.xml_output, "iobuffer_size", BYTES_FMT, iobufsize);
    options.xml_output = print_xml_tag_str(pool,options.xml_output, "iosizes_sequential", sequential_requestsizes);
    options.xml_output = print_xml_tag_str(pool,options.xml_output, "iosizes_random", random_requestsizes);
    options.xml_output = print_xml_tag_str(pool,options.xml_output, "queue_depths", depths);

    options.xml_output = print_xml_tag_open(pool, options.xml_output, "workers");
    for(i=0; i < worker_array->nelts; ++i) {
        options.xml_output = print_xml_tag_open(pool, options.xml_output, "worker");
        printf("%-26s %s\n",apr_psprintf(pool,"Worker %d:",i),
            apr_psprintf(pool,"%s of size %s",workers[i]->filename,
                print_size(pool, "%.0f%cB", workers[i]->filesize, K)));
        options.xml_output = print_xml_tag_number(pool, options.xml_output, "id", i);
        options.xml_output = print_xml_tag_str(pool, options.xml_output, "filename", workers[i]->filename);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "size", BYTES_FMT, workers[i]->filesize);
        options.xml_output = print_xml_tag_close(pool, options.xml_output, "worker");
    }
    options.xml_output = print_xml_tag_close(pool, options.xml_output, "workers");

    printf("%-26s %s\n", "Bytes written:", print_size(pool, BYTES_FMT, (double) bytes_written, K));
    printf("%-26s %s\n", "Bytes read:", print_size(pool, BYTES_FMT, (double) bytes_read, K));
    printf("%-26s %s\n", "Total requests:", print_size(pool, REQUEST_FMT, (double) total_requests, K));
    printf("%-26s %s\n", "Total time:", print_time(pool, end_time - start_time));
    printf("%-26s %s\n", "Overall score:", print_size(pool, THROUGHPUT_FMT, (double) overall_score, K));

    printf("\n");

    printf("%-25s  %12s  %12s  %12s  %10s  %10s  %11s\n",
           "", "Base","Max","Min","Bytes","Bytes","Time");
    printf("%-25s  %12s  %12s  %12s  %10s  %10s  %11s\n",
           "Workload","Throughput","Throughput","Throughput","Written","Read", "Elapsed");
    print_statistics_seperator(pool);

    options.xml_output = print_xml_tag_open(pool, options.xml_output, "test_summary");

   for(i=0; i < options.statistics_array->nelts; ++i) {
        statistics = APR_ARRAY_IDX(options.statistics_array, i, struct io_statistics*);
        double weighted_throughput = statistics->accumulated_weighted_bytes_per_second/statistics->accumulated_weight;
        printf("%-25s  %12s  %12s  %12s  %10s  %10s  %11s\n",
        statistics->description,
        print_size(pool, THROUGHPUT_FMT, weighted_throughput, K),
        print_size(pool, THROUGHPUT_FMT, statistics->max_throughput, 1024),
        print_size(pool, THROUGHPUT_FMT, statistics->min_throughput, 1024),
        print_size(pool, BYTES_FMT, (double) statistics->bytes_written, 1024),
        print_size(pool, BYTES_FMT, (double) statistics->bytes_read, 1024),
        print_time(pool, statistics->elapsed));
        options.xml_output = print_xml_tag_open(pool, options.xml_output, "test");
        options.xml_output = print_xml_tag_str(pool, options.xml_output, "description", statistics->description);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "weighted_throughput", THROUGHPUT_FMT, weighted_throughput);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "max_throughput", THROUGHPUT_FMT, statistics->max_throughput);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "min_throughput", THROUGHPUT_FMT, statistics->min_throughput);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "bytes_written", BYTES_FMT, statistics->bytes_written);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "bytes_read", BYTES_FMT, statistics->bytes_read);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "write_request", REQUEST_FMT, statistics->write_requests);
        options.xml_output = print_xml_tag_size(pool, options.xml_output, "read_requests", REQUEST_FMT, statistics->read_requests);
        options.xml_output = print_xml_tag_number(pool, options.xml_output, "max_concurrent_iops", statistics->max_active);
        options.xml_output = print_xml_tag_time(pool, options.xml_output, "time_spent", statistics->elapsed);


        options.xml_output = print_xml_tag_close(pool, options.xml_output, "test");
    }
    options.xml_output = print_xml_tag_close(pool, options.xml_output, "test_summary");

    print_statistics_seperator(pool);
	printf("\nCleaning up\n");
	rv = destroy_workers(workers, worker_array->nelts, pool);
	assert(rv == APR_SUCCESS);

    options.xml_output = print_xml_tag_size(pool, options.xml_output, "bytes_written", BYTES_FMT, bytes_written);
    options.xml_output = print_xml_tag_size(pool, options.xml_output, "bytes_read", BYTES_FMT, bytes_read);
    options.xml_output = print_xml_tag_size(pool, options.xml_output, "write_requests", REQUEST_FMT, write_requests);
    options.xml_output = print_xml_tag_size(pool, options.xml_output, "read_requests", REQUEST_FMT, read_requests);
    options.xml_output = print_xml_tag_time(pool, options.xml_output, "total_time", end_time - start_time);
    options.xml_output = print_xml_tag_size(pool, options.xml_output, "overall_score", THROUGHPUT_FMT, overall_score);

	options.xml_output = print_xml_tag_close(pool, options.xml_output, "summary");
	options.xml_output = print_xml_tag_close(pool, options.xml_output, "diskBench");

    if(xml_file != NULL) {
        apr_file_puts(options.xml_output,xml_file);
        apr_file_close(xml_file);
    }

	apr_pool_destroy(pool);

	return 0;
}
