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

/*
 * Ethernet PHY: Microchip/Micrel KSZ9031RNX
 */
static int phy_fixup(struct phy_device *dev)
{
	return 0;
}

static int socfpga_init(void)
{
	if (!of_machine_is_compatible("altr,socfpga-cyclone5"))
		return 0;

	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9031, MICREL_PHY_ID_MASK, phy_fixup);

	return 0;
}
console_initcall(socfpga_init);
