obj-y := time.o cpu.o system.o irq-common.o common.o

obj-$(CONFIG_COMMON_CLK) += clk-pllv1.o clk-pllv2.o clk-pllv3.o clk-gate2.o clk-pfd.o clk-busy.o clk.o clk-fixup-div.o clk-fixup-mux.o clk-gate-exclusive.o

ifeq ($(CONFIG_CPU_IDLE),y)
AFLAGS_imx6ul_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6UL) += cpuidle-imx6ul.o imx6ul_low_power_idle.o
AFLAGS_imx6ull_low_power_idle.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6ULL) += imx6ull_low_power_idle.o
endif

ifdef CONFIG_SND_IMX_SOC
obj-y += ssi-fiq.o
obj-y += ssi-fiq-ksym.o
endif

obj-$(CONFIG_HAVE_IMX_ANATOP) += anatop.o
obj-$(CONFIG_HAVE_IMX_GPC) += gpc.o
obj-$(CONFIG_HAVE_IMX_MMDC) += mmdc.o
obj-$(CONFIG_HAVE_IMX_SRC) += src.o
ifneq ($(CONFIG_SOC_IMX6),)
AFLAGS_headsmp.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SMP) += headsmp.o platsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
endif
obj-y += busfreq_lpddr2.o busfreq-imx.o busfreq_ddr3.o
AFLAGS_ddr3_freq_imx6sx.o :=-Wa,-march=armv7-a
AFLAGS_lpddr2_freq_imx6sx.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6UL) += clk-imx6ul.o mach-imx6ul.o ddr3_freq_imx6sx.o lpddr2_freq_imx6sx.o

ifeq ($(CONFIG_SUSPEND),y)
AFLAGS_suspend-imx6.o :=-Wa,-march=armv7-a
obj-$(CONFIG_SOC_IMX6) += suspend-imx6.o
endif
obj-$(CONFIG_SOC_IMX6) += pm-imx6.o

obj-y += devices/
