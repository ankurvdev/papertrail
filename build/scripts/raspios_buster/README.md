# Links
[Raspberry Pi OS Buster 2020-12-04](https://downloads.raspberrypi.org/raspios_lite_armhf/images/raspios_lite_armhf-2020-12-04/2020-12-02-raspios-buster-armhf-lite.zip)

[Cross Compiler GCC-10.2](
https://iweb.dl.sourceforge.net/project/raspberry-pi-cross-compilers/Raspberry%20Pi%20GCC%20Cross-Compiler%20Toolchains/Buster/GCC%2010.2.0/Raspberry%20Pi%201%2C%20Zero/cross-gcc-10.2.0-pi_0-1.tar.gz)

[Qemu Kernel](https://raw.githubusercontent.com/dhruvvyas90/qemu-rpi-kernel/master/kernel-qemu-5.4.51-buster)
+
[DTB](https://raw.githubusercontent.com/dhruvvyas90/qemu-rpi-kernel/master/versatile-pb-buster-5.4.51.dtb)

# QEMU
```bash
sudo su
apt update && apt install libglib2.0-dev libusb-1.0-0-dev
```

# Mounted fixes

```bash
if [[ ! -e ${mount}/usr/lib/crt1.o ]]; then ln -s arm-linux-gnueabihf/crt1.o ${mount}/usr/lib/crt1.o; fi 
if [[ ! -e ${mount}/usr/lib/crti.o ]]; then ln -s arm-linux-gnueabihf/crti.o ${mount}/usr/lib/crti.o; fi 
if [[ ! -e ${mount}/usr/lib/crtn.o ]]; then ln -s arm-linux-gnueabihf/crtn.o ${mount}/usr/lib/crtn.o; fi 

python ~/scripts/broken_absolute_links_to_relative.py ${mount}
```

# PKG CONFIG fixes

```bash
# merge lib and libs.private
vim ${mount}/usr/lib/arm-linux-gnueabihf/pkgconfig/libusb-1.0.pc
```
