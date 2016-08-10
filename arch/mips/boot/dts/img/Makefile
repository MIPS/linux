dtb-$(CONFIG_FIT_IMAGE_FDT_BOSTON)	+= boston.dtb

# Force kbuild to make empty built-in.o if necessary
obj-					+= dummy.o

always					:= $(dtb-y)
clean-files				:= *.dtb *.dtb.S
