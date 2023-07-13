scriptdir=$(realpath $(dirname $0))
imagefile=$(ls ${scriptdir}/*-raspios-buster-armhf-lite.img)
sudo umount ${scriptdir}/sysroot_mount
qemu-system-arm \
	-M versatilepb -cpu arm1176 -m 256 \
	-drive file=${imagefile},if=none,index=0,media=disk,format=raw,id=disk0 \
	-device virtio-blk-pci,drive=disk0,disable-modern=on,disable-legacy=off \
	-dtb ${scriptdir}/versatile-pb-buster-5.4.51.dtb \
	-kernel ${scriptdir}/kernel-qemu-5.4.51-buster \
	-append 'root=/dev/vda2' -no-reboot
