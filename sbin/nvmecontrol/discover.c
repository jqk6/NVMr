/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2019 Dell Inc. or its subsidiaries. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioccom.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <paths.h>
#include <sysexits.h>

#include "nvmecontrol.h"
#include "comnd.h"

/* Tables for command line parsing */

static cmd_fn_t discover;

static struct options {
	char *ipaddr;
	char *port;
} opt = {
	.ipaddr = NULL,
	.port = NULL,
};


static const struct opts discover_opts[] = {
#define OPT(l, s, t, opt, addr, desc) { l, s, t, &opt.addr, desc }
	OPT("ipaddr", 'i', arg_string, opt, ipaddr,
	    "IP address"),
	OPT("port", 'p', arg_string, opt, port,
	    "RDMA protocol port number"),
	{ NULL, 0, arg_none, NULL, NULL }
};
#undef OPT

static struct cmd discover_cmd = {
	.name = "discover",
	.fn = discover,
	.descr = "List NVMe SubNQN over RDMA fabrics",
	.ctx_size = sizeof(opt),
	.opts = discover_opts,
};

CMD_COMMAND(discover_cmd);

/* End of tables for command line parsing */

#define NVMR_DEV_PATH (_PATH_DEV NVMR_DEV)
#define NUMDLE        62

void
discover(const struct cmd *f, int argc, char *argv[])
{
	size_t optlen;
	nvmr_ioctl_t nvmr_ioctl;
	char *traddrp, *trsvcidp;
	int fd, retval, count;
	struct nvmr_discovery_log_entry *dlep;
	struct {
		struct nvmr_discovery_log_page  hdr;
		struct nvmr_discovery_log_entry dlearr[NUMDLE];
	} __packed logbuf;

	nvmr_ioctl.nvmri_pi.nvmrpi_ip = NULL;
	nvmr_ioctl.nvmri_pi.nvmrpi_port = NULL;

	if (arg_parse(argc, argv, f))
		return;

	if ((opt.ipaddr == NULL) || (opt.port == NULL)) {
		arg_help(argc, argv, f);
		/* should not reach here */
	}

	optlen = strlen(opt.ipaddr);
	if (optlen > MAX_IP_STR_LEN) {
		errx(EX_USAGE, "IP address too long");
	}
	nvmr_ioctl.nvmri_ipstrlen = optlen + 1; /* Final \0 */
	nvmr_ioctl.nvmri_pi.nvmrpi_ip = opt.ipaddr;

	optlen = strlen(opt.port);
	if (optlen > MAX_PORT_STR_LEN) {
		errx(EX_USAGE, "Port number too long");
	}
	nvmr_ioctl.nvmri_portstrlen = optlen + 1; /* Final \0 */
	nvmr_ioctl.nvmri_pi.nvmrpi_port = opt.port;

	if ((nvmr_ioctl.nvmri_pi.nvmrpi_ip == NULL) ||
	    (nvmr_ioctl.nvmri_pi.nvmrpi_port == NULL)) {
		errx(EX_USAGE, "Both IP address and port-number not specified");
	}

	fd = open(NVMR_DEV_PATH,  O_RDONLY);
	if (fd < 0) {
		err(EX_OSERR, "Could not open \"%s\"", NVMR_DEV_PATH);
	}

	nvmr_ioctl.nvmri_retlen = sizeof(logbuf);
	nvmr_ioctl.nvmri_retbuf = &logbuf;

	retval = ioctl(fd, NVMR_DISCOVERY, &nvmr_ioctl);
	if (retval < 0) {
		err(EX_OSERR, "DISCOVERY ioctl failed");
	}

	for (count = 0; (count < NUMDLE) && ((uint64_t)count <
	    logbuf.hdr.nvmrdlp_numrec); count++) {
		dlep = logbuf.dlearr + count;
		if (dlep->nvmrdle_trtype == TRTYPE_RDMA) {
			traddrp = dlep->nvmrdle_traddr;
			trsvcidp = dlep->nvmrdle_trsvcid;

			printf("%s,%s,%s\n", strsep(&traddrp, " \n"),
			    strsep(&trsvcidp, " \n"), dlep->nvmrdle_subnqn);
		}
	}
	if (logbuf.hdr.nvmrdlp_numrec > (uint64_t)count) {
		warnx("More controllers available than listed\n");
	}

	close(fd);
	exit(0);
}
