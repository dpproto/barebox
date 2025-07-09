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

#include <debug_ll.h>

/*
 * Ethernet PHY: Microchip/Micrel KSZ9031RNX
 */
static int phy_fixup(struct phy_device *dev)
{
	puts_ll("board.c: phy_fixup() >> start\n");
	/*
	 * min rx data delay, max rx/tx clock delay,
	 * min rx/tx control delay
	 */
	phy_write_mmd(dev, MDIO_MMD_WIS, 4, 0);
	phy_write_mmd(dev, MDIO_MMD_WIS, 5, 0);
	phy_write_mmd(dev, MDIO_MMD_WIS, 8, 0x003ff);
	puts_ll("board.c: phy_fixup() >> end\n");
	return 0;
}

static int socfpga_init(void)
{
	puts_ll("board.c: socfpga_init() >> start\n");
	if (!of_machine_is_compatible("altr,socfpga-cyclone5"))
		return 0;
	puts_ll("board.c: socfpga_init() >> compat OK\n");


	if (IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9031, MICREL_PHY_ID_MASK, phy_fixup);

	puts_ll("board.c: socfpga_init() >> end\n");
	return 0;
}
console_initcall(socfpga_init);
