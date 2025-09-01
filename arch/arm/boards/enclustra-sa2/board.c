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
#include "atsha204a.h"
#include "si5338_config.h"

/** Enclustra's MAC address vendor prefix is 20:B0:F7 */
#define ENCLUSTRA_PREFIX			(0x20b0f7)

/*
 * Ethernet PHY: Microchip/Micrel KSZ9031RNX
 */
static int phy_fixup(struct phy_device *dev)
{
	return 0;
}

static void set_mac_addr(void)
{
	uint8_t hwaddr[6] = {0, 0, 0, 0, 0, 0};
	uint32_t hwaddr_prefix;
	/* backup MAC addresses, used if the actual one can't be read from EEPROM: */
	const uint8_t enclustra_ethaddr_def1[] = { 0x20, 0xB0, 0xF7, 0x01, 0x02, 0x03 };
	/* 2nd backup MAC address if required later
	const uint8_t enclustra_ethaddr_def2[] = { 0x20, 0xB0, 0xF7, 0x01, 0x02, 0x04 };
	*/

	if (atsha204_get_mac(hwaddr)) {
		printf("%s() >> ERROR: can't read MAC address from EEPROM, using default address\n", __FUNCTION__);
		eth_register_ethaddr(0, enclustra_ethaddr_def1);
		return;
	}

	debug("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
			hwaddr[0], hwaddr[1], hwaddr[2],
			hwaddr[3], hwaddr[4], hwaddr[5]);

	/* check vendor prefix and set the environment variable */
	hwaddr_prefix = (hwaddr[0] << 16) | (hwaddr[1] << 8) | (hwaddr[2]);
	if (hwaddr_prefix == ENCLUSTRA_PREFIX) {
		eth_register_ethaddr(0, hwaddr);
	}
	else {
		printf("ERROR: invalid MAC address vendor prefix, using default address\n");
		eth_register_ethaddr(0, enclustra_ethaddr_def1);
	}
}

static int socfpga_init(void)
{
	if (!of_machine_is_compatible("enclustra,mercury-sa2"))
		return 0;

	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9031, MICREL_PHY_ID_MASK, phy_fixup);

	set_mac_addr();

#ifdef CONFIG_MACH_SOCFPGA_ENCLUSTRA_SA2_SI5338
	/* configure clock generator on the Enclustra ST1 baseboard: */
	si5338_init();
#endif

	return 0;
}
late_initcall(socfpga_init);
