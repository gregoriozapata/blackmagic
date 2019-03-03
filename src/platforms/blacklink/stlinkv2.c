/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2019 Uwe Bonnes
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.	 If not, see <http://www.gnu.org/licenses/>.
 */
/* Much code and ideas shamelessly taken form
 * https://github.com/texane/stlink.git
 * git://git.code.sf.net/p/openocd/code
 * https://github.com/pavelrevak/pystlink
 * https://github.com/pavelrevak/pystlink
 *
 * with some contribution.
 */
#include "general.h"
#include "gdb_if.h"
#include "version.h"
#include "stlinkv2.h"

#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>

#define VENDOR_ID_STLINK		0x483
#define PRODUCT_ID_STLINK_MASK	0xfff0
#define PRODUCT_ID_STLINK_GROUP 0x3740
#define PRODUCT_ID_STLINKV1		0x3744
#define PRODUCT_ID_STLINKV2		0x3748
#define PRODUCT_ID_STLINKV21	0x374b
#define PRODUCT_ID_STLINKV21_MSD 0x3752
#define PRODUCT_ID_STLINKV3		0x374f

#define STLINK_SWIM_ERR_OK             0x00
#define STLINK_SWIM_BUSY               0x01
#define STLINK_DEBUG_ERR_OK            0x80
#define STLINK_DEBUG_ERR_FAULT         0x81
#define STLINK_JTAG_GET_IDCODE_ERROR   0x09
#define STLINK_SWD_AP_WAIT             0x10
#define STLINK_SWD_AP_FAULT            0x11
#define STLINK_SWD_AP_ERROR            0x12
#define STLINK_SWD_AP_PARITY_ERROR     0x13
#define STLINK_JTAG_WRITE_ERROR        0x0c
#define STLINK_JTAG_WRITE_VERIF_ERROR  0x0d
#define STLINK_SWD_DP_WAIT             0x14
#define STLINK_SWD_DP_FAULT            0x15
#define STLINK_SWD_DP_ERROR            0x16
#define STLINK_SWD_DP_PARITY_ERROR     0x17

#define STLINK_SWD_AP_WDATA_ERROR      0x18
#define STLINK_SWD_AP_STICKY_ERROR     0x19
#define STLINK_SWD_AP_STICKYORUN_ERROR 0x1a

#define STLINK_CORE_RUNNING            0x80
#define STLINK_CORE_HALTED             0x81
#define STLINK_CORE_STAT_UNKNOWN       -1

#define STLINK_GET_VERSION             0xF1
#define STLINK_DEBUG_COMMAND           0xF2
#define STLINK_DFU_COMMAND             0xF3
#define STLINK_SWIM_COMMAND            0xF4
#define STLINK_GET_CURRENT_MODE        0xF5
#define STLINK_GET_TARGET_VOLTAGE      0xF7

#define STLINK_DEV_DFU_MODE            0x00
#define STLINK_DEV_MASS_MODE           0x01
#define STLINK_DEV_DEBUG_MODE          0x02
#define STLINK_DEV_SWIM_MODE           0x03
#define STLINK_DEV_BOOTLOADER_MODE     0x04
#define STLINK_DEV_UNKNOWN_MODE        -1

#define STLINK_DFU_EXIT                0x07

#define STLINK_SWIM_ENTER                  0x00
#define STLINK_SWIM_EXIT                   0x01
#define STLINK_SWIM_READ_CAP               0x02
#define STLINK_SWIM_SPEED                  0x03
#define STLINK_SWIM_ENTER_SEQ              0x04
#define STLINK_SWIM_GEN_RST                0x05
#define STLINK_SWIM_RESET                  0x06
#define STLINK_SWIM_ASSERT_RESET           0x07
#define STLINK_SWIM_DEASSERT_RESET         0x08
#define STLINK_SWIM_READSTATUS             0x09
#define STLINK_SWIM_WRITEMEM               0x0a
#define STLINK_SWIM_READMEM                0x0b
#define STLINK_SWIM_READBUF                0x0c

#define STLINK_DEBUG_GETSTATUS             0x01
#define STLINK_DEBUG_FORCEDEBUG            0x02
#define STLINK_DEBUG_APIV1_RESETSYS        0x03
#define STLINK_DEBUG_APIV1_READALLREGS     0x04
#define STLINK_DEBUG_APIV1_READREG         0x05
#define STLINK_DEBUG_APIV1_WRITEREG        0x06
#define STLINK_DEBUG_READMEM_32BIT         0x07
#define STLINK_DEBUG_WRITEMEM_32BIT        0x08
#define STLINK_DEBUG_RUNCORE               0x09
#define STLINK_DEBUG_STEPCORE              0x0a
#define STLINK_DEBUG_APIV1_SETFP           0x0b
#define STLINK_DEBUG_READMEM_8BIT          0x0c
#define STLINK_DEBUG_WRITEMEM_8BIT         0x0d
#define STLINK_DEBUG_APIV1_CLEARFP         0x0e
#define STLINK_DEBUG_APIV1_WRITEDEBUGREG   0x0f
#define STLINK_DEBUG_APIV1_SETWATCHPOINT   0x10

#define STLINK_DEBUG_ENTER_JTAG_RESET      0x00
#define STLINK_DEBUG_ENTER_SWD_NO_RESET    0xa3
#define STLINK_DEBUG_ENTER_JTAG_NO_RESET   0xa4

#define STLINK_DEBUG_APIV1_ENTER           0x20
#define STLINK_DEBUG_EXIT                  0x21
#define STLINK_DEBUG_READCOREID            0x22

#define STLINK_DEBUG_APIV2_ENTER           0x30
#define STLINK_DEBUG_APIV2_READ_IDCODES    0x31
#define STLINK_DEBUG_APIV2_RESETSYS        0x32
#define STLINK_DEBUG_APIV2_READREG         0x33
#define STLINK_DEBUG_APIV2_WRITEREG        0x34
#define STLINK_DEBUG_APIV2_WRITEDEBUGREG   0x35
#define STLINK_DEBUG_APIV2_READDEBUGREG    0x36

#define STLINK_DEBUG_APIV2_READALLREGS     0x3A
#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS 0x3B
#define STLINK_DEBUG_APIV2_DRIVE_NRST      0x3C

#define STLINK_DEBUG_APIV2_GETLASTRWSTATUS2 0x3E

#define STLINK_DEBUG_APIV2_START_TRACE_RX  0x40
#define STLINK_DEBUG_APIV2_STOP_TRACE_RX   0x41
#define STLINK_DEBUG_APIV2_GET_TRACE_NB    0x42
#define STLINK_DEBUG_APIV2_SWD_SET_FREQ    0x43
#define STLINK_DEBUG_APIV2_JTAG_SET_FREQ   0x44
#define STLINK_DEBUG_APIV2_READ_DAP_REG    0x45
#define STLINK_DEBUG_APIV2_WRITE_DAP_REG   0x46
#define STLINK_DEBUG_APIV2_READMEM_16BIT   0x47
#define STLINK_DEBUG_APIV2_WRITEMEM_16BIT  0x48

#define STLINK_DEBUG_APIV2_INIT_AP         0x4B
#define STLINK_DEBUG_APIV2_CLOSE_AP_DBG    0x4C

#define STLINK_APIV3_SET_COM_FREQ           0x61
#define STLINK_APIV3_GET_COM_FREQ           0x62

#define STLINK_APIV3_GET_VERSION_EX         0xFB

#define STLINK_DEBUG_APIV2_DRIVE_NRST_LOW   0x00
#define STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH  0x01
#define STLINK_DEBUG_APIV2_DRIVE_NRST_PULSE 0x02


#define STLINK_TRACE_SIZE               4096
#define STLINK_TRACE_MAX_HZ             2000000

#define STLINK_V3_MAX_FREQ_NB               10

/** */
enum stlink_mode {
	STLINK_MODE_UNKNOWN = 0,
	STLINK_MODE_DFU,
	STLINK_MODE_MASS,
	STLINK_MODE_DEBUG_JTAG,
	STLINK_MODE_DEBUG_SWD,
	STLINK_MODE_DEBUG_SWIM
};

typedef struct {
	libusb_context* libusb_ctx;
	uint16_t     vid;
	uint16_t     pid;
	uint8_t      serial[32];
	uint8_t      ver_stlink;
	uint8_t      ver_api;
	uint8_t      ver_jtag;
	uint8_t      ver_mass;
	uint8_t      ver_swim;
	uint8_t      ver_bridge;
	uint16_t     block_size;
	libusb_device_handle *handle;
	struct libusb_transfer* req_trans;
	struct libusb_transfer* rep_trans;
} stlink;

stlink Stlink;

static void exit_function(void)
{
	libusb_exit(NULL);
	DEBUG("Cleanup\n");
}

/* SIGTERM handler. */
static void sigterm_handler(int sig)
{
	(void)sig;
	exit(0);
}

struct trans_ctx {
#define TRANS_FLAGS_IS_DONE (1 << 0)
#define TRANS_FLAGS_HAS_ERROR (1 << 1)
    volatile unsigned long flags;
};

int debug_level = 0;

static void on_trans_done(struct libusb_transfer * trans)
{
    struct trans_ctx * const ctx = trans->user_data;

    if (trans->status != LIBUSB_TRANSFER_COMPLETED)
    {
		DEBUG("on_trans_done: ");
        if(trans->status == LIBUSB_TRANSFER_TIMED_OUT)
        {
            DEBUG("Timeout\n");
        }
        else if (trans->status == LIBUSB_TRANSFER_CANCELLED)
            DEBUG("cancelled\n");
        else if (trans->status == LIBUSB_TRANSFER_NO_DEVICE)
            DEBUG("no device\n");
        else
            DEBUG("unknown\n");
        ctx->flags |= TRANS_FLAGS_HAS_ERROR;
    }
    ctx->flags |= TRANS_FLAGS_IS_DONE;
}

static int submit_wait(struct libusb_transfer * trans) {
	struct timeval start;
	struct timeval now;
	struct timeval diff;
	struct trans_ctx trans_ctx;
	enum libusb_error error;

	trans_ctx.flags = 0;

	/* brief intrusion inside the libusb interface */
	trans->callback = on_trans_done;
	trans->user_data = &trans_ctx;

	if ((error = libusb_submit_transfer(trans))) {
		DEBUG("libusb_submit_transfer(%d): %s\n", error,
			  libusb_strerror(error));
		exit(-1);
	}

	gettimeofday(&start, NULL);

	while (trans_ctx.flags == 0) {
		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		if (libusb_handle_events_timeout(Stlink.libusb_ctx, &timeout)) {
			DEBUG("libusb_handle_events()\n");
			return -1;
		}

		gettimeofday(&now, NULL);
		timersub(&now, &start, &diff);
		if (diff.tv_sec >= 1) {
			libusb_cancel_transfer(trans);
			DEBUG("libusb_handle_events() timeout\n");
			return -1;
		}
	}

	if (trans_ctx.flags & TRANS_FLAGS_HAS_ERROR) {
		DEBUG("libusb_handle_events() | has_error\n");
		return -1;
	}

	return 0;
}

static int send_recv(uint8_t *txbuf, size_t txsize,
					 uint8_t *rxbuf, size_t rxsize)
{
	int res = 0;
	int ep_tx = 1;
	if( txsize) {
		libusb_fill_bulk_transfer(Stlink.req_trans, Stlink.handle,
								  ep_tx | LIBUSB_ENDPOINT_OUT,
								  txbuf, txsize,
								  NULL, NULL,
								  0
			);
		DEBUG_USB("  Send (%ld): ", txsize);
		for (size_t z = 0; z < txsize && z < 32 ; z++)
			DEBUG_USB("%02x", txbuf[z]);
		if (submit_wait(Stlink.req_trans)) {
			DEBUG_USB("clear 2\n");
			libusb_clear_halt(Stlink.handle,2);
			return -1;
		}
	}
	/* send_only */
	if (rxsize != 0) {
		/* read the response */
		libusb_fill_bulk_transfer(Stlink.rep_trans, Stlink.handle,
								  0x01| LIBUSB_ENDPOINT_IN,
								  rxbuf, rxsize, NULL, NULL, 0);

		if (submit_wait(Stlink.rep_trans)) {
			DEBUG("clear 1\n");
			libusb_clear_halt(Stlink.handle,1);
			return -1;
		}
		res = Stlink.rep_trans->actual_length;
		if (res >0) {
			int i;
			uint8_t *p = rxbuf;
			DEBUG_USB(" Rec (%ld/%d)", rxsize, res);
			for (i = 0; i < res && i < 32 ; i++)
				DEBUG_USB("%02x", p[i]);
		}
	}
	DEBUG_USB("\n");
	return res;
}

/**
    Converts an STLINK status code held in the first byte of a response to
	readable error
*/
static int stlink_usb_error_check(uint8_t *data)
{
	switch (data[0]) {
		case STLINK_DEBUG_ERR_OK:
			return STLINK_ERROR_OK;
		case STLINK_DEBUG_ERR_FAULT:
			DEBUG("SWD fault response (0x%x)\n", STLINK_DEBUG_ERR_FAULT);
			return STLINK_ERROR_FAIL;
		case STLINK_JTAG_GET_IDCODE_ERROR:
			DEBUG("Failure reading IDCODE\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_AP_WAIT:
			DEBUG("wait status SWD_AP_WAIT (0x%x)\n", STLINK_SWD_AP_WAIT);
			return STLINK_ERROR_WAIT;
		case STLINK_SWD_DP_WAIT:
			DEBUG("wait status SWD_DP_WAIT (0x%x)\n", STLINK_SWD_DP_WAIT);
			return STLINK_ERROR_WAIT;
		case STLINK_JTAG_WRITE_ERROR:
			DEBUG("Write error\n");
			return STLINK_ERROR_FAIL;
		case STLINK_JTAG_WRITE_VERIF_ERROR:
			DEBUG("Write verify error, ignoring\n");
			return STLINK_ERROR_OK;
		case STLINK_SWD_AP_FAULT:
			/* git://git.ac6.fr/openocd commit 657e3e885b9ee10
			 * returns STLINK_ERROR_OK with the comment:
			 * Change in error status when reading outside RAM.
			 * This fix allows CDT plugin to visualize memory.
			 */
			DEBUG("STLINK_SWD_AP_FAULT\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_AP_ERROR:
			DEBUG("STLINK_SWD_AP_ERROR\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_AP_PARITY_ERROR:
			DEBUG("STLINK_SWD_AP_PARITY_ERROR\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_DP_FAULT:
			DEBUG("STLINK_SWD_DP_FAULT\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_DP_ERROR:
			DEBUG("STLINK_SWD_DP_ERROR\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_DP_PARITY_ERROR:
			DEBUG("STLINK_SWD_DP_PARITY_ERROR\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_AP_WDATA_ERROR:
			DEBUG("STLINK_SWD_AP_WDATA_ERROR\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_AP_STICKY_ERROR:
			DEBUG("STLINK_SWD_AP_STICKY_ERROR\n");
			return STLINK_ERROR_FAIL;
		case STLINK_SWD_AP_STICKYORUN_ERROR:
			DEBUG("STLINK_SWD_AP_STICKYORUN_ERROR\n");
			return STLINK_ERROR_FAIL;
		default:
			DEBUG("unknown/unexpected STLINK status code 0x%x\n", data[0]);
			return STLINK_ERROR_FAIL;
	}
}

static void stlink_version(void)
{
	uint8_t cmd[16] = {STLINK_GET_VERSION};
	uint8_t data[16];
    int size = send_recv(cmd, 1, data, 6);
    if (size == -1) {
        printf("[!] send_recv STLINK_GET_VERSION\n");
    }
	Stlink.vid = data[3] << 8 | data[2];
	Stlink.pid = data[5] << 8 | data[4];
	int  version = data[0] << 8 | data[1];
	Stlink.ver_stlink = (version >> 12) & 0x0f;
	if (Stlink.ver_stlink == 3) {
		cmd[0] = STLINK_APIV3_GET_VERSION_EX;
		int size = send_recv(cmd, 16, data, 16);
		if (size == -1) {
			printf("[!] send_recv STLINK_APIV3_GET_VERSION_EX\n");
		}
		Stlink.ver_swim  =  data[1];
		Stlink.ver_jtag  =  data[2];
		Stlink.ver_mass  =  data[3];
		Stlink.ver_bridge = data[4];
		Stlink.block_size = 512;
	} else {
		Stlink.ver_jtag  =  (version >>  6) & 0x3f;
		if ((Stlink.pid == PRODUCT_ID_STLINKV21_MSD) ||
			(Stlink.pid == PRODUCT_ID_STLINKV21)) {
			Stlink.ver_mass  =  (version >>  0) & 0x3f;
		}
		Stlink.block_size = 64;
	}
	DEBUG("V%dJ%d",Stlink.ver_stlink, Stlink.ver_jtag);
	if (Stlink.ver_api == 30)
		DEBUG("M%dB%dS%d", Stlink.ver_mass, Stlink.ver_bridge, Stlink.ver_swim);
	else if (Stlink.ver_api == 20)
 		DEBUG("S%d", Stlink.ver_swim);
	else if (Stlink.ver_api == 21)
 		DEBUG("M%d", Stlink.ver_mass);
	DEBUG("\n");
}

void stlink_leave_state(void)
{
	uint8_t cmd[2] = {STLINK_GET_CURRENT_MODE};
	uint8_t data[2];
	send_recv(cmd, 1, data, 2);
	if (data[0] == STLINK_DEV_DFU_MODE) {
		uint8_t dfu_cmd[2] = {STLINK_DFU_COMMAND, STLINK_DFU_EXIT};
		DEBUG("Leaving DFU Mode\n");
		send_recv(dfu_cmd, 2, NULL, 0);
	} else if (data[0] == STLINK_DEV_SWIM_MODE) {
		uint8_t swim_cmd[2] = {STLINK_SWIM_COMMAND, STLINK_SWIM_EXIT};
		DEBUG("Leaving SWIM Mode\n");
		send_recv(swim_cmd, 2, NULL, 0);
	} else if (data[0] == STLINK_DEV_DEBUG_MODE) {
		uint8_t dbg_cmd[2] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_EXIT};
		DEBUG("Leaving DEBUG Mode\n");
		send_recv(dbg_cmd, 2, NULL, 0);
	} else if (data[0] == STLINK_DEV_BOOTLOADER_MODE) {
		DEBUG("BOOTLOADER Mode\n");
	} else if (data[0] == STLINK_DEV_MASS_MODE) {
		DEBUG("MASS Mode\n");
	} else {
		DEBUG("Unknown Mode\n");
	}
}

const char *stlink_target_voltage(void)
{
	uint8_t cmd[2] = {STLINK_GET_TARGET_VOLTAGE};
	uint8_t data[8];
	send_recv(cmd, 1, data, 8);
	uint16_t adc[2];
	adc[0] = data[0] | data[1] << 8; /* Calibration value? */
	adc[1] = data[4] | data[5] << 8; /* Measured value?*/
	float result  = 0.0;
	if (adc[0])
		result = 2.0 * adc[1] * 1.2 / adc[0];
	static char res[6];
	sprintf(res, "%4.2fV", result);
	return res;
}
void stlink_init(int argc, char **argv)
{
	libusb_device **devs, *dev;
	int r;
	atexit(exit_function);
	signal(SIGTERM, sigterm_handler);
	signal(SIGINT, sigterm_handler);
	libusb_init(&Stlink.libusb_ctx);
	char *serial = NULL;
	int c;
	while((c = getopt(argc, argv, "s:v:")) != -1) {
		switch(c) {
		case 's':
			serial = optarg;
			break;
		case 'v':
			if (optarg)
				debug_level = atoi(optarg);
			break;
		}
	}
	r = libusb_init(NULL);
	if (r < 0)
		DEBUG("Failed: %s", libusb_strerror(r));
	ssize_t cnt = libusb_get_device_list(NULL, &devs);
	if (cnt < 0) {
		libusb_exit(NULL);
		DEBUG("Failed: %s", libusb_strerror(r));
		goto error;
	}
	int i = 0;
	bool multiple_devices = false;
	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0) {
			fprintf(stderr, "libusb_get_device_descriptor failed %s",
					libusb_strerror(r));
			goto error;
		}
		if ((desc.idVendor == VENDOR_ID_STLINK) &&
			((desc.idProduct & PRODUCT_ID_STLINK_MASK) ==
			 PRODUCT_ID_STLINK_GROUP)) {
			if (Stlink.handle) {
				libusb_close(Stlink.handle);
				multiple_devices = (serial)? false : true;
			}
			r = libusb_open(dev, &Stlink.handle);
			if (r == LIBUSB_SUCCESS) {
				if (desc.iSerialNumber) {
					r = libusb_get_string_descriptor_ascii
						(Stlink.handle,desc.iSerialNumber, Stlink.serial,
						 sizeof(Stlink.serial));
				} else {
					DEBUG("No serial number\n");
				}
				if (serial && (!strcmp((char*)Stlink.serial, serial)) &&
					(desc.idProduct == PRODUCT_ID_STLINKV1))
					DEBUG("Found ");
				if (((!serial) || (!strcmp((char*)Stlink.serial, serial))) &&
					desc.idProduct == PRODUCT_ID_STLINKV2) {
					DEBUG("STLINKV2	 serial %s\n", Stlink.serial);
					Stlink.ver_api = 20;
				} else if (((!serial) ||
						  (!strcmp((char*)Stlink.serial, serial))) &&
						 desc.idProduct == PRODUCT_ID_STLINKV21) {
					Stlink.ver_api = 21;
					DEBUG("STLINKV21 serial %s\n", Stlink.serial);
				} else if (((!serial) ||
							(!strcmp((char*)Stlink.serial, serial))) &&
						   desc.idProduct == PRODUCT_ID_STLINKV3) {
					DEBUG("STLINKV3	serial %s\n", Stlink.serial);
					Stlink.ver_api = 21;
				} else if (((!serial) ||
							(!strcmp((char*)Stlink.serial, serial))) &&
						   desc.idProduct == PRODUCT_ID_STLINKV1) {
					DEBUG("STLINKV1 serial %s not supported\n", Stlink.serial);
				}
				if (serial && (!strcmp((char*)Stlink.serial, serial)))
					break;
			} else {
				DEBUG("Open failed %s\n", libusb_strerror(r));
			}
		}
	}
	if (multiple_devices) {
		DEBUG("Multiple Stlinks. Please sepecify serial number\n");
		goto error_1;
	}
	if (!Stlink.handle) {
		DEBUG("No Stlink device found!\n");
		goto error;
	}
	int config;
	r = libusb_get_configuration(Stlink.handle, &config);
	if (r) {
		DEBUG("libusb_get_configuration failed %d: %s", r, libusb_strerror(r));
		goto error_1;
	}
	DEBUG("Config %d\n", config);
	if (config != 1) {
		r = libusb_set_configuration(Stlink.handle, 0);
		if (r) {
			DEBUG("libusb_set_configuration failed %d: %s",
				  r, libusb_strerror(r));
			goto error_1;
		}
	}
	r = libusb_claim_interface(Stlink.handle, 0);
	if (r)
	{
		DEBUG("libusb_claim_interface failed %s", libusb_strerror(r));
		goto error_1;
	}
	Stlink.req_trans = libusb_alloc_transfer(0);
	Stlink.rep_trans = libusb_alloc_transfer(0);
 	stlink_version();
	if (Stlink.ver_api < 30 && Stlink.ver_jtag < 32) {
		DEBUG("Please update Firmware\n");
		goto error_1;
	}
	stlink_leave_state();
	assert(gdb_if_init() == 0);
	return;
  error_1:
	libusb_close(Stlink.handle);
  error:
	libusb_free_device_list(devs, 1);
	exit(-1);
}

void stlink_srst_set_val(bool assert)
{
	uint8_t cmd[3] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_DRIVE_NRST,
					  (assert)? STLINK_DEBUG_APIV2_DRIVE_NRST_LOW
					  : STLINK_DEBUG_APIV2_DRIVE_NRST_HIGH};
	uint8_t data[2];
	send_recv(cmd, 3, data, 2);
	stlink_usb_error_check(data);
}

bool stlink_set_freq_divisor(uint16_t divisor)
{
	uint8_t cmd[4] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_SWD_SET_FREQ,
					  divisor & 0xff, divisor >> 8};
	uint8_t data[2];
	send_recv(cmd, 4, data, 2);
	if (stlink_usb_error_check(data))
		return false;
	return true;
}

int stlink_enter_debug_swd(void)
{
	stlink_set_freq_divisor(1);
	uint8_t cmd[3] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_ENTER,
					  STLINK_DEBUG_ENTER_SWD_NO_RESET};
	uint8_t data[2];
	DEBUG("Enter SWD\n");
	send_recv(cmd, 3, data, 2);
	return stlink_usb_error_check(data);
}

uint32_t stlink_read_coreid(void)
{
	uint8_t cmd[2] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_READ_IDCODES};
	uint8_t data[12];
	send_recv(cmd, 2, data, 12);
	stlink_usb_error_check(data);
	uint32_t id =  data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
	DEBUG("Read Core ID: 0x%08" PRIx32 "\n", id);
	return id;
}

static uint8_t dap_select = 0;
int stlink_read_dp_register(uint16_t port, uint16_t addr, uint32_t *res)
{
	uint8_t cmd[16] = {STLINK_DEBUG_COMMAND,
					  STLINK_DEBUG_APIV2_READ_DAP_REG,
					  port & 0xff,
					  port >> 8,
					  0, addr >> 8};
	if (port == STLINK_DEBUG_PORT_ACCESS  && dap_select)
		cmd[4] = ((dap_select & 0xf) << 4) | (addr & 0xf);
	else
		cmd[4] = addr & 0xff;
	uint8_t data[8];
	send_recv(cmd, 16, data, 8);
	stlink_usb_error_check(data);
	uint32_t ret = data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
	DEBUG_STLINK("Read DP, Addr 0x%04" PRIx16 ": 0x%08" PRIx32" \n",
		  addr, ret);
	*res = ret;
	return stlink_usb_error_check(data);
}

int stlink_write_dp_register(uint16_t port, uint16_t addr, uint32_t val)
{
	if (port == STLINK_DEBUG_PORT_ACCESS && addr == 8) {
		dap_select = val;
		DEBUG_STLINK("Caching SELECT 0x%02" PRIx32 "\n", val);
		return STLINK_ERROR_OK;
	} else {
		uint8_t cmd[16] = {
			STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_WRITE_DAP_REG,
			port & 0xff, port >> 8,
			addr & 0xff, addr >> 8,
			val & 0xff, (val >>  8) & 0xff, (val >> 16) & 0xff,
			(val >> 24) & 0xff};
		uint8_t data[2];
		send_recv(cmd, 16, data, 2);
		DEBUG_STLINK("Write DP, Addr 0x%04" PRIx16 ": 0x%08" PRIx32
			  " \n", addr, val);
		return stlink_usb_error_check(data);
	}
}

int stlink_open_ap(uint8_t ap)
{
       uint8_t cmd[3] = {
               STLINK_DEBUG_COMMAND,
               STLINK_DEBUG_APIV2_INIT_AP,
               ap,
       };
       uint8_t data[2];
       send_recv(cmd, 3, data, 2);
       DEBUG_STLINK("Open AP %d\n", ap);
       return stlink_usb_error_check(data);
}

void stlink_close_ap(uint8_t ap)
{
       uint8_t cmd[3] = {
               STLINK_DEBUG_COMMAND,
               STLINK_DEBUG_APIV2_CLOSE_AP_DBG,
               ap,
       };
       uint8_t data[2];
       send_recv(cmd, 3, data, 2);
       DEBUG_STLINK("Close AP %d\n", ap);
       stlink_usb_error_check(data);
}
int stlink_usb_get_rw_status(void)
{
	uint8_t cmd[2] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_APIV2_GETLASTRWSTATUS2
	};
	uint8_t data[12];
	send_recv(cmd, 2, data, 12);
	return stlink_usb_error_check(data);
}

void stlink_readmem(void *dest, uint32_t src, size_t len)
{
	uint8_t type;
	char *CMD;
	if (src & 1 || len & 1) {
		CMD = "READMEM_8BIT";
		type = STLINK_DEBUG_READMEM_8BIT;
		if (len > Stlink.block_size) {
			DEBUG(" Too large!\n");
			return;
		}
	} else if (src & 3 || len & 3) {
		CMD = "READMEM_16BIT";
		type = STLINK_DEBUG_APIV2_READMEM_16BIT;
	} else {
		CMD = "READMEM_32BIT";
		type = STLINK_DEBUG_READMEM_32BIT;

	}
	DEBUG_STLINK("%s len %" PRI_SIZET " addr 0x%08" PRIx32 ": ", CMD, len, src);
	uint8_t cmd[8] = {
		STLINK_DEBUG_COMMAND,
		type,
		src & 0xff, (src >>  8) & 0xff, (src >> 16) & 0xff,
		(src >> 24) & 0xff,
		len & 0xff, len >> 8};
	send_recv(cmd, 8, dest, len);
	uint8_t *p = (uint8_t*)dest;
	for (size_t i = 0; i > len ; i++) {
		DEBUG_STLINK("%02x", *p++);
	}
	DEBUG_STLINK("\n");
	stlink_usb_get_rw_status();
}

void stlink_writemem8(uint32_t addr, size_t len, uint8_t *buffer)
{
	DEBUG_STLINK("Mem Write8 len %" PRI_SIZET " addr 0x%08" PRIx32 ": ", len, addr);
	for (size_t t = 0; t < len; t++) {
		DEBUG_STLINK("%02x", buffer[t]);
	}
	while (len) {
		size_t length;
		if (len > Stlink.block_size)
			length = Stlink.block_size;
		else
			length = len;
		uint8_t cmd[8] = {
			STLINK_DEBUG_COMMAND,
			STLINK_DEBUG_WRITEMEM_8BIT,
			addr & 0xff, (addr >>  8) & 0xff, (addr >> 16) & 0xff,
			(addr >> 24) & 0xff,
			length & 0xff, length >> 8};
		send_recv(cmd, 8, NULL, 0);
		send_recv((void*)buffer, length, NULL, 0);
		stlink_usb_get_rw_status();
		len -= length;
		addr += length;
	}
}

void stlink_writemem16(uint32_t addr, size_t len, uint16_t *buffer)
{
	DEBUG_STLINK("Mem Write16 len %" PRI_SIZET " addr 0x%08" PRIx32 ": ", len, addr);
	for (size_t t = 0; t < len; t++) {
		DEBUG_STLINK("%04x", buffer[t]);
	}
	uint8_t cmd[8] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_APIV2_WRITEMEM_16BIT,
		addr & 0xff, (addr >>  8) & 0xff, (addr >> 16) & 0xff,
		(addr >> 24) & 0xff,
		len & 0xff, len >> 8};
	send_recv(cmd, 8, NULL, 0);
	send_recv((void*)buffer, len, NULL, 0);
	stlink_usb_get_rw_status();
}

void stlink_writemem32(uint32_t addr, size_t len, uint32_t *buffer)
{
	DEBUG_STLINK("Mem Write32 len %" PRI_SIZET " addr 0x%08" PRIx32 ": ", len, addr);
	for (size_t t = 0; t < len; t++) {
		DEBUG_STLINK("%04x", buffer[t]);
	}
	uint8_t cmd[8] = {
		STLINK_DEBUG_COMMAND,
		STLINK_DEBUG_WRITEMEM_32BIT,
		addr & 0xff, (addr >>  8) & 0xff, (addr >> 16) & 0xff,
		(addr >> 24) & 0xff,
		len & 0xff, len >> 8};
	send_recv(cmd, 8, NULL, 0);
	send_recv((void*)buffer, len, NULL, 0);
	stlink_usb_get_rw_status();
}

void stlink_regs_read(void *data)
{
	uint8_t cmd[8] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READALLREGS};
	uint8_t res[88];
	DEBUG_STLINK("Read all core registers\n");
	send_recv(cmd, 8, res, 88);
	stlink_usb_error_check(res);
	memcpy(data, res + 4, 84);
}

uint32_t stlink_reg_read(int num)
{
	uint8_t cmd[8] = {STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_READREG, num};
	uint8_t res[8];
	send_recv(cmd, 8, res, 8);
	stlink_usb_error_check(res);
	uint32_t ret = res[0] | res[1] << 8 | res[2] << 16 | res[3] << 24;
	DEBUG_STLINK("Read reg %02" PRId32 " val 0x%08" PRIx32 "\n", num, ret);
	return ret;
}

void stlink_reg_write(int num, uint32_t val)
{
	uint8_t cmd[7] = {
		STLINK_DEBUG_COMMAND, STLINK_DEBUG_APIV2_WRITEREG, num,
		val & 0xff, (val >>  8) & 0xff, (val >> 16) & 0xff,
		(val >> 24) & 0xff
	};
	uint8_t res[2];
	send_recv(cmd, 7, res, 2);
	DEBUG_STLINK("Write reg %02" PRId32 " val 0x%08" PRIx32 "\n", num, val);
	stlink_usb_error_check(res);
}
