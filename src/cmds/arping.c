/**
 * @file
 * @brief Ping hosts by ARP requests/replies.
 *
 * @date 23.12.09
 * @author Nikolay Korotky
 */

#include <embox/cmd.h>

#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <netutils.h>
#include <net/arp.h>

EMBOX_CMD(exec);

static void print_usage(void) {
	printf("Usage: arping [-I if] [-c cnt] host\n");
}

static int exec(int argc, char **argv) {
	int opt;
	int cnt = 4, cnt_resp = 0, i, j;
	in_device_t *in_dev = inet_dev_find_by_name("eth0");
	struct in_addr dst;
	char *dst_b, *from_b;
	struct in_addr from;
	unsigned char mac[18];

	getopt_init();
	while (-1 != (opt = getopt(argc, argv, "I:c:h"))) {
		switch (opt) {
		case 'I': /* get interface */
			if (NULL == (in_dev = inet_dev_find_by_name(optarg))) {
				TRACE("arping: unknown iface %s\n", optarg);
				return -1;
			}
			break;
		case 'c': /* get ping cnt */
			if (1 != sscanf(optarg, "%d", &cnt)) {
				TRACE("arping: bad number of packets to transmit.\n");
				return -1;
			}
			break;
		case '?':
		case 'h':
			print_usage();
			/* FALLTHROUGH */
		default:
			return 0;
		}
	};

	if (argc == 1) {
		print_usage();
		return 0;
	}

	/* Get destination address. */
	if (0 == inet_aton(argv[argc - 1], &dst)) {
		printf("arping: invalid IP address: %s\n", argv[argc - 1]);
		return -1;
	}

	dst_b = inet_ntoa(dst);
	from.s_addr = htonl(in_dev->ifa_address);
	from_b = inet_ntoa(from);
	printf("ARPING %s from %s %s\n", dst_b, from_b, in_dev->dev->name);
	for (i = 1; i <= cnt; i++) {
		arp_delete_entity(NULL, dst.s_addr, NULL);
		arp_send(ARPOP_REQUEST, ETH_P_ARP, dst.s_addr, in_dev->dev,
				in_dev->ifa_address, NULL, (in_dev->dev)->dev_addr, NULL);
		usleep(ARP_RES_TIME);
		if (-1 != (j = arp_lookup(in_dev, dst.s_addr))) {
			macaddr_print(mac, arp_tables[j].hw_addr);
			printf("Unicast reply from %s [%s]  %dms\n", dst_b, mac, 0);
			cnt_resp++;
		}
	}
	printf("Sent %d probes (%d broadcast(s))\n", cnt, 1);
	printf("Received %d response(s)\n", cnt_resp);
	free(dst_b);
	free(from_b);
	return 0;
}
