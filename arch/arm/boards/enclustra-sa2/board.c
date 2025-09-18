// SPDX-License-Identifier: GPL-2.0-only

#include <common.h>
#include <types.h>
#include <driver.h>
#include <init.h>
#include <asm/armlinux.h>
#include <linux/mdio.h>
#include <linux/micrel_phy.h>
#include <linux/phy.h>
#include <linux/sizes.h>
#include <fcntl.h>
#include <fs.h>
#include <mach/socfpga/cyclone5-regs.h>
#include <net.h>
#include <linux/nvmem-consumer.h>
#include <of_net.h>
#include "si5338_config.h"

/** Enclustra's MAC address vendor prefix is 20:B0:F7 */
#define ENCLUSTRA_PREFIX            (0x20b0f7)
#define SERIAL_NUMBER_NUM_BYTES     (4)

/*
 * Ethernet PHY: Microchip/Micrel KSZ9031RNX
 */
static int phy_fixup(struct phy_device *dev)
{
	return 0;
}

/*
 * Read the MAC address via the atsha204a driver.
 *
 * Set two consecutive MAC addresses, as specified by the manufacturer.
 */
static void set_mac_addr(void)
{
	uint8_t hwaddr[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
	uint32_t hwaddr_prefix;
	static const char * const aliases[] = { "ethernet0" };
	struct device_node *np, *root;
	/* Fallback MAC addresses, used if we can't read from EEPROM: */
	const uint8_t enclustra_ethaddr_fallback1[] = { 0x20, 0xB0, 0xF7, 0x01,
													0x02, 0x03 };
	const uint8_t enclustra_ethaddr_fallback2[] = { 0x20, 0xB0, 0xF7, 0x01,
													0x02, 0x04 };

	root = of_get_root_node();
	for (int i = 0; i < ARRAY_SIZE(aliases); i++) {
		const char *alias = aliases[i];

		np = of_find_node_by_alias(root, alias);
		if (!np) {
			pr_warn("%s() >> ERROR: can't find alias %s\n", __func__, alias);
			continue;
		}
		if (of_get_mac_addr_nvmem(np, hwaddr) != 0) {
			printf("%s() >> ERROR: can't read MAC address from NVMEM\n",
				   __func__);
			goto fallback_addr;
		}
	}

	debug("MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		  hwaddr[0], hwaddr[1], hwaddr[2],
		  hwaddr[3], hwaddr[4], hwaddr[5]);

	/* check vendor prefix and set the environment variable */
	hwaddr_prefix = (hwaddr[0] << 16) | (hwaddr[1] << 8) | (hwaddr[2]);
	if (hwaddr_prefix == ENCLUSTRA_PREFIX) {
		eth_register_ethaddr(0, hwaddr);
		hwaddr[5]++;    /* calculate 2nd, consecutive MAC address */
		eth_register_ethaddr(1, hwaddr);
	} else {
		printf("%s() >> ERROR: invalid MAC address vendor prefix,"
			   "using fallback addresses\n", __func__);
		goto fallback_addr;
	}

	return;

fallback_addr:
	eth_register_ethaddr(0, enclustra_ethaddr_fallback1);
	eth_register_ethaddr(1, enclustra_ethaddr_fallback2);
}

static int socfpga_init(void)
{
	if (!of_machine_is_compatible("enclustra,mercury-sa2"))
		return 0;

	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9031, MICREL_PHY_ID_MASK,
								   phy_fixup);

	set_mac_addr();

	return 0;
}
late_initcall(socfpga_init);
