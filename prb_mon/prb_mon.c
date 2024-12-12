#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <unistd.h>

#include "ranbooster_common.h"

#define MAX_COMRESSION_LEVEL_INGRESS (5)
#define MAX_COMRESSION_LEVEL_EGRESS (3)

#define PATH_MAX 512

// TODO: Re-write
int open_bpf_map_file(const char *filename,
		      struct bpf_map_info *info)
{
	int err,    fd;
	__u32 info_len = sizeof(*info);

	/* Lesson#1: There is only a weak dependency to libbpf here as
	 * bpf_obj_get is a simple wrapper around the bpf-syscall
	 */
	fd = bpf_obj_get(filename);
	if (fd < 0) {
		fprintf(stderr,
			"WARN: Failed to open bpf map file:%s err(%d):%s\n",
			filename, errno, strerror(errno));
		return fd;
	}

	if (info) {
		err = bpf_obj_get_info_by_fd(fd, info, &info_len);
		if (err) {
			fprintf(stderr, "ERR: %s() can't get info - %s\n",
				__func__,  strerror(errno));
			return -1;
		}
	}

	return fd;
}

int process_prb_event(void *ctx, void *data, size_t data_sz) {
    struct prb_stats_event *e = data;
    struct prb_stats_event *stats = ctx;

    stats->num_prbs_used += e->num_prbs_used;
    stats->total_num_prbs += e->total_num_prbs;

    //printf("Total prbs %d and used prbs %d\n", e->total_num_prbs, e->num_prbs_used);
    return 0;
}

int main(int argc, char **argv) {
    char *mon_load_ingress_path, *mon_load_egress_path;
    struct bpf_map_info ingress_info = { 0 };
    struct bpf_map_info egress_info = { 0 };
    struct ring_buffer *ingress_rb = NULL;
    struct ring_buffer *egress_rb = NULL;
    double ingress_prb_util, egress_prb_util;
    struct prb_stats_event egress_stats = {0};
    struct prb_stats_event ingress_stats = {0};

    int ingress_load_fd, egress_load_fd;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s egress_load_map ingress_load_map\n", argv[0]);
        return 1;
    }

    mon_load_egress_path = argv[1];
    mon_load_ingress_path = argv[2];

    ingress_load_fd = open_bpf_map_file(mon_load_ingress_path, &ingress_info);
	if (ingress_load_fd < 0) {
		return -1;
	}

    egress_load_fd = open_bpf_map_file(mon_load_egress_path, &egress_info);
	if (egress_load_fd < 0) {
		return -1;
	}

    ingress_rb = ring_buffer__new(ingress_load_fd, process_prb_event, &ingress_stats, NULL);
    if (!ingress_rb) {
        perror("Failed to create ring buffer");
        return 1;
    }

    egress_rb = ring_buffer__new(egress_load_fd, process_prb_event, &egress_stats, NULL);
    if (!egress_rb) {
        perror("Failed to create ring buffer");
        return 1;
    }

    while(1) {
        // if (bpf_map_lookup_elem(ingress_load_fd, &key, &ingress_load) != 0) {
        //     printf("Error fetching ingress load stats\n");
        // }

        int ret = ring_buffer__poll(ingress_rb, 1 /* infinite timeout */);
        if (ret < 0) {
            fprintf(stderr, "Error polling ring buffer: %d\n", ret);
            break;
        }

        if (ingress_stats.total_num_prbs == 0) {
            ingress_prb_util = 0;
        } else {
            ingress_prb_util = ((double)ingress_stats.num_prbs_used / ingress_stats.total_num_prbs) * 100;
        }

        //printf("Average PRB utilization %.2f\n", ();
        memset(&ingress_stats, 0, sizeof(struct prb_stats_event));

        ret = ring_buffer__poll(egress_rb, 1 /* infinite timeout */);
        if (ret < 0) {
            fprintf(stderr, "Error polling ring buffer: %d\n", ret);
            break;
        }

        if (egress_stats.total_num_prbs == 0) {
            egress_prb_util = 0;
        } else {
            egress_prb_util = ((double)egress_stats.num_prbs_used / egress_stats.total_num_prbs) * 100;
        }

        memset(&egress_stats, 0, sizeof(struct prb_stats_event));

        printf("Average uplink PRB utilization: %.2f%%, Average downlink PRB utilization: %.2f%%\n", ingress_prb_util, egress_prb_util);


        sleep(1);
        //usleep(1000);
    }

}