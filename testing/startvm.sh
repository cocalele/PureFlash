#!/bin/bash
ARGS=($@)
OSDISK=$1.qcow2

VMDIR="/$1"

rm -rf /$1
#	echo "copy VM file ..." 
#	cp -rp  /vm-centos8/ $VMDIR
#set -v
#nohup /root/v2/qemu-system-aarch64  -cpu host -M virt,gic-version=max  -enable-kvm -smp 8 -m 8G \
#-drive if=virtio,file=$VMDIR/centos8-with-iostat.qcow2   \
#-nic user,hostfwd=tcp::$2-:22 -vnc :$(($2-9100)) \
#-device virtio-gpu-pci -bios $VMDIR/QEMU_EFI-pflash.raw  \
# -drive format=raw,file=pfbd:$3,if=virtio &> qemu.log &
  

mkdir /$1
cd $VMDIR
qemu-img create -f qcow2 -F qcow2  -b  /vm-centos8/centos8-with-iostat.qcow2 $OSDISK
#qemu-img info $OSDISK
cp /root/v2/qemu/pc-bios/efi-virtio.rom .
cp /usr/share/qemu/keymaps/en-us .
cp /vm-centos8/QEMU_EFI-pflash.raw . 

set -v
nohup /root/v2/qemu-system-aarch64  -cpu host -M virt,gic-version=max  -enable-kvm -smp 8 -m 8G \
-drive if=virtio,file=$VMDIR/$OSDISK   \
-nic user,hostfwd=tcp::$2-:22 -vnc :$(($2-20022)) \
-device virtio-gpu-pci -bios $VMDIR/QEMU_EFI-pflash.raw  \
 -drive format=raw,file=pfbd:$3,if=virtio &> qemu.log &
  
