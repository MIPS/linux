/dts-v1/;

/ {
	description = KERNEL_NAME;
	#address-cells = <ADDR_CELLS>;

	images {
		kernel@0 {
			description = KERNEL_NAME;
			data = /incbin/(VMLINUX_BINARY);
			type = "kernel";
			arch = "mips";
			os = "linux";
			compression = VMLINUX_COMPRESSION;
			load = /bits/ ADDR_BITS <VMLINUX_LOAD_ADDRESS>;
			entry = /bits/ ADDR_BITS <VMLINUX_ENTRY_ADDRESS>;
			VMLINUX_ENDIANNESS;
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		default = "conf@default";

		conf@default {
			description = "Generic Linux kernel";
			kernel = "kernel@0";
		};
	};
};

#ifdef CONFIG_FIT_IMAGE_FDT_BOSTON
/ {
	images {
		fdt@boston {
			description = "img,boston Device Tree";
			data = /incbin/("boot/dts/img/boston.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};

		fdt@boston-sleipnir {
			description = "img,boston-sleipnir Device Tree";
			data = /incbin/("boot/dts/img/boston-sleipnir.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		conf@boston {
			description = "Boston Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@boston";
		};

		conf@boston-sleipnir {
			description = "Boston + Sleipnir Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@boston-sleipnir";
		};
	};
};
#endif /* CONFIG_FIT_IMAGE_FDT_BOSTON */

#ifdef CONFIG_FIT_IMAGE_FDT_CI40
/ {
	images {
		fdt@ci40 {
			description = "img,pistachio-marduk (Ci40) Device Tree";
			data = /incbin/("boot/dts/img/pistachio_marduk.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
			hash@0 {
				algo = "sha1";
			};
		};
	};

	configurations {
		conf@ci40 {
			description = "Creator Ci40 Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@ci40";
		};
	};
};
#endif /* CONFIG_FIT_IMAGE_FDT_CI40 */
