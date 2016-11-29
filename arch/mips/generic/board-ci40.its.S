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
