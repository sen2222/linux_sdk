#
# SPDX-License-Identifier:	GPL-2.0+
#

# mx6ull_sen_emmc uses board C code and does not embed a device tree.
targets += $(dtb-y)
DTC_FLAGS +=

PHONY += dtbs
dtbs: $(addprefix $(obj)/, $(dtb-y))
	@:

clean-files := *.dtb
