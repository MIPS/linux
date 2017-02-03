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
