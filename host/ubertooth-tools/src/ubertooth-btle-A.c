/*
 * Copyright 2010, 2011 Michael Ossmann
 *
 * This file is part of Project Ubertooth.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ubertooth.h"
#include <ctype.h>
#include <err.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#ifdef ENABLE_PCAP
#include <pcap.h>
extern pcap_t *pcap_dumpfile;
extern pcap_dumper_t *dumper;
#endif // ENABLE_PCAP

struct libusb_device_handle *devh = NULL;
extern FILE *infile;
extern FILE *dumpfile;


int convert_mac_address(char *s, uint8_t *o) {
	int i;
	// validate length
	if (strlen(s) != 6 * 2 + 5) {
		printf("Error: MAC address is wrong length\n");
		return 0;
	}

	// validate hex chars and : separators
	for (i = 0; i < 6*3; i += 3) {
		if (!isxdigit(s[i]) ||
			!isxdigit(s[i+1])) {
			printf("Error: MAC address contains invalid character(s)\n");
			return 0;
		}
		if (i < 5*3 && s[i+2] != ':') {
			printf("Error: MAC address contains invalid character(s)\n");
			return 0;
		}
	}
	// sanity: checked; convert
	for (i = 0; i < 6; ++i) {
		unsigned byte;
		sscanf(&s[i*3], "%02x",&byte);
		o[i] = byte;
	}
	return 1;
}

static void usage(void)
{
	printf("ubertooth-btle - passive Bluetooth Low Energy monitoring\n");
	printf("Usage:\n");
	printf("\t-h this help\n");
	printf("\n");
	printf("    Major modes:\n");
	printf("\t-f follow connections\n");
	printf("\t-p promiscuous: sniff active connections\n");
	printf("\t-a[address] get/set access address (example: -a8e89bed6)\n");
	printf("\t-s<address> faux slave mode, using MAC addr (example: -s22:44:66:88:aa:cc)\n");
	printf("\t-t<address> set connection following target (example: -t22:44:66:88:aa:cc)\n");
	printf("\n");
	printf("    Data source:\n");
	printf("\t-i<filename> read packets from file\n");
	printf("\t-U<0-7> set ubertooth device to use\n");
	printf("\n");
	printf("    Misc:\n");
	printf("\t-r<filename> capture packets to PCAPNG file\n");
#ifdef ENABLE_PCAP
	printf("\t-q<filename> capture packets to PCAP file (DLT_BLUETOOTH_LE_LL_WITH_PHDR)\n");
	printf("\t-c<filename> capture packets to PCAP file (DLT_PPI)\n");
#endif
	printf("\t-d<filename> dump packets to binary file\n");
	printf("\t-A<index> advertising channel index (default 37)\n");
	printf("\t-v[01] verify CRC mode, get status or enable/disable\n");
	printf("\t-x<n> allow n access address offenses (default 32)\n");
	printf("\t-b Transmit ba2a ya H\n");
	printf("\t-R<address> Receive Address by H,(example: -t22:44:66:88:aa:cc)\n");

	printf("\nIf an input file is not specified, an Ubertooth device is used for live capture.\n");
	printf("In get/set mode no capture occurs.\n");


}

void cleanup(int sig)
{
	sig = sig;
	if (devh) {
		ubertooth_stop(devh);
	}
	exit(0);
}


u32 btle_calc_crc(u32 crc_init, u8 *data, int len) {
	u32 state = crc_init & 0xffffff;
	u32 lfsr_mask = 0x5a6000; // 010110100110000000000000
	int i, j;

	for (i = 0; i < len; ++i) {
		u8 cur = data[i];
		for (j = 0; j < 8; ++j) {
			int next_bit = (state ^ cur) & 1;
			cur >>= 1;
			state >>= 1;
			if (next_bit) {
				state |= 1 << 23;
				state ^= lfsr_mask;
			}
		}
	}

	return state;
}


void temp_transmit(uint8_t *mac_address){
	u32 calc_crc;
	int i;


	u8 adv_ind[] = {
		// LL header
		0x00, 0x08,

		// advertising address
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

		// advertising data
	    0x1D, 0x3F,

		// CRC (calc)4a 5f 3b
		0xff, 0xff, 0xff,
	};

	u8 adv_ind_len = sizeof(adv_ind)-3;

	// copy the user-specified mac address

	   for (i = 0; i < 6; ++i)
		 adv_ind[i+2] = mac_address[5-i];


	calc_crc = btle_calc_crc(0xAAAAAA, adv_ind, adv_ind_len);
	adv_ind[adv_ind_len+0] = (calc_crc >>  0) & 0xff;
	adv_ind[adv_ind_len+1] = (calc_crc >>  8) & 0xff;
	adv_ind[adv_ind_len+2] = (calc_crc >> 16) & 0xff;

	printf("1-Data(%d):",adv_ind_len);
	for(i = 0;i < adv_ind_len+3; i++){
		printf("%02x ",adv_ind[i]);

	}
	printf("\n");

	calc_crc = btle_calc_crc(0x555555, adv_ind, adv_ind_len);
	adv_ind[adv_ind_len+0] = (calc_crc >>  0) & 0xff;
	adv_ind[adv_ind_len+1] = (calc_crc >>  8) & 0xff;
	adv_ind[adv_ind_len+2] = (calc_crc >> 16) & 0xff;

	printf("2-Data(%d):",adv_ind_len);
	for(i = 0;i < adv_ind_len+3; i++){
		printf("%02x ",adv_ind[i]);

	}
	printf("\n");

}


int main(int argc, char *argv[])
{
	int i;
	int opt;
	int do_follow, do_file, do_promisc;
	int do_get_aa, do_set_aa;
	int do_crc;
	int do_adv_index;
	int do_slave_mode;
	int do_target;
	int do_rx_node;
	int do_transmit;
	char ubertooth_device = -1;

	btle_options cb_opts = { .allowed_access_address_errors = 32 };

	int r;
	u32 access_address;
	uint8_t mac_address[6] = { 0, };
	uint8_t rx_address[6] = { 0, };

	do_follow = do_file = 0, do_promisc = 0;
	do_get_aa = do_set_aa = 0;
	do_crc = -1; // 0 and 1 mean set, 2 means get
	do_adv_index = 37;
	do_slave_mode = do_target = 0;
	do_transmit = 0;
	do_rx_node = 0;

	while ((opt=getopt(argc,argv,"a::r:d:ahfpi:U:v::A:s:t:x:c:q:bR:")) != EOF) {
		switch(opt) {
		case 'a':
			if (optarg == NULL) {
				do_get_aa = 1;
			} else {
				do_set_aa = 1;
				sscanf(optarg, "%08x", &access_address);
			}
			break;
		case 'f':
			do_follow = 1;
			break;
		case 'p':
			do_promisc = 1;
			break;
		case 'i':
			do_file = 1;
			infile = fopen(optarg, "r");
			if (infile == NULL) {
				printf("Could not open file %s\n", optarg);
				usage();
				return 1;
			}
			break;
		case 'U':
			ubertooth_device = atoi(optarg);
			break;
		case 'r':
			if (!h_pcapng_le) {
				if (lell_pcapng_create_file(optarg, "Ubertooth", &h_pcapng_le)) {
					err(1, "lell_pcapng_create_file: ");
				}
			}
			else {
				printf("Ignoring extra capture file: %s\n", optarg);
			}
			break;
#ifdef ENABLE_PCAP
		case 'q':
			if (!h_pcap_le) {
				if (lell_pcap_create_file(optarg, &h_pcap_le)) {
					err(1, "lell_pcap_create_file: ");
				}
			}
			else {
				printf("Ignoring extra capture file: %s\n", optarg);
			}
			break;
		case 'c':
			if (!h_pcap_le) {
				if (lell_pcap_ppi_create_file(optarg, 0, &h_pcap_le)) {
					err(1, "lell_pcap_ppi_create_file: ");
				}
			}
			else {
				printf("Ignoring extra capture file: %s\n", optarg);
			}
			break;
#endif
		case 'd':
			dumpfile = fopen(optarg, "w");
			if (dumpfile == NULL) {
				perror(optarg);
				return 1;
			}
			break;
		case 'v':
			if (optarg)
				do_crc = atoi(optarg) ? 1 : 0;
			else
				do_crc = 2; // get
			break;
		case 'A':
			do_adv_index = atoi(optarg);
			if (do_adv_index < 37 || do_adv_index > 39) {
				printf("Error: advertising index must be 37, 38, or 39\n");
				usage();
				return 1;
			}
			break;
		case 's':
			do_slave_mode = 1;
			r = convert_mac_address(optarg, mac_address);
			if (!r) {
				usage();
				return 1;
			}
			break;
		case 't':
			do_target = 1;
			r = convert_mac_address(optarg, mac_address);
			if (!r) {
				usage();
				return 1;
			}
			break;
		case 'b':
			do_transmit=1;
			break;
		case 'R':
			do_rx_node=1;
			r = convert_mac_address(optarg, rx_address);
			if (!r) {
				usage();
				return 1;
			}
			break;
		case 'x':
			cb_opts.allowed_access_address_errors = (unsigned) atoi(optarg);
			if (cb_opts.allowed_access_address_errors > 32) {
				printf("Error: can tolerate 0-32 access address bit errors\n");
				usage();
				return 1;
			}
			break;
		case 'h':
		default:
			//usage();
			return 1;
		}
	}

	if (do_file) {
		rx_btle_file(infile);
		fclose(infile);
		return 0; // do file is the only command that doesn't open ubertooth
	}

	devh = ubertooth_start(ubertooth_device);
	if (devh == NULL) {
		usage();
		return 1;
	}

	/* Clean up on exit. */
	signal(SIGINT, cleanup);
	signal(SIGQUIT, cleanup);
	signal(SIGTERM, cleanup);


	if (do_follow || do_promisc) {
		usb_pkt_rx pkt;

		cmd_set_modulation(devh, MOD_BT_LOW_ENERGY);

		if (do_follow) {
			u16 channel;
			if (do_adv_index == 37)
				channel = 2402;
			else if (do_adv_index == 38)
				channel = 2426;
			else
				channel = 2480;
			cmd_set_channel(devh, channel);
			cmd_btle_sniffing(devh, 2);
		} else {
			cmd_btle_promisc(devh);
		}

		while (1) {
//			printf("Poll\n");
			int r = cmd_poll(devh, &pkt);
			if (r < 0) {
				printf("USB error\n");
				break;
			}
			if (r == sizeof(usb_pkt_rx)){
				printf("Waiting\n");
				cb_btle(&cb_opts, &pkt, 0);
			}
			usleep(10);
		}
		ubertooth_stop(devh);
	}

	if (do_get_aa) {
		access_address = cmd_get_access_address(devh);
		printf("Access address: %08x\n", access_address);
		return 0;
	}

	if (do_set_aa) {
		cmd_set_access_address(devh, access_address);
		printf("access address set to: %08x\n", access_address);
	}


	if (do_crc >= 0) {
		int r;
		if (do_crc == 2) {
			r = cmd_get_crc_verify(devh);
		} else {
			cmd_set_crc_verify(devh, do_crc);
			r = do_crc;
		}
		printf("CRC: %sverify\n", r ? "" : "DO NOT ");
	}

	if (do_slave_mode) {
		u16 channel;
		if (do_adv_index == 37)
			channel = 2402;
		else if (do_adv_index == 38)
			channel = 2426;
		else
			channel = 2480;
		cmd_set_channel(devh, channel);

		cmd_btle_slave(devh, mac_address);
	}

	if (do_target) {
		r = cmd_btle_set_target(devh, mac_address);
		if (r == 0) {
			int i;
			printf("target set to: ");
			for (i = 0; i < 5; ++i)
				printf("%02x:", mac_address[i]);
			printf("%02x\n", mac_address[5]);
		}
	}

	/*
	 * Ahmed Salem
	 */
	if(do_transmit){
		usb_pkt_rx pkt;

		cmd_set_modulation(devh, MOD_BT_LOW_ENERGY);


		u16 channel;

		if (do_adv_index == 37)
			channel = 2402;
		else if (do_adv_index == 38)
			channel = 2426;
		else
			channel = 2480;


		//channel = 2440;

		r = cmd_set_channel(devh, channel);
		if (r != 0) {
					printf("channel setting error error#:%d\n",r);
		}else{
			printf("current channel:%d\n",cmd_get_channel(devh));
		}

		printf("Access address in H: %08x\n", cmd_get_access_address(devh));

		int i=0;
		printf("Mac Address: ");
		for (i = 0; i < 5; ++i)
			printf("%02x:", mac_address[i]);
		printf("%02x\n", mac_address[5]);

		//temp_transmit(mac_address);
		int x=0;

		printf("Tx_RX\n");
		r = cmd_h_btle_tx(devh, mac_address);
		if (r != 0) {
			printf("Tx error#:%d\n",r);
	//		break;
		}

//		while(1){
////			//printf("Rx\n");
//			int r = cmd_poll(devh, &pkt);
////			if (r < 0) {
////				printf("USB error\n");
////				break;
////			}else{
////				printf(".");
////			}
//			usleep(10);
////			if (r == sizeof(usb_pkt_rx)){
////				printf("Waiting\n");
////				cb_btle_h(&cb_opts, &pkt, 0, rx_address);
////			}
//		}

		//printf("Start Tx...\n");
		//tx_live(devh,NULL,0);
//		ubertooth_stop(devh);

	}


	return 0;
}
