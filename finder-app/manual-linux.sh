#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
FINDER_APP_DIR=$(pwd)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # Build the kernel
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper    # Deep clean
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig   # Configure default config for "virt"
    #make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all     # Build kernel image
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules     # Build kernel modules
    #make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs        # Build device tree
fi

echo "Adding the Image in outdir"
cd "$OUTDIR"
pwd
cp linux-stable/arch/${ARCH}/boot/Image Image

echo "Creating the staging directory for the root filesystem"
if [ -d "${OUTDIR}/rootfs" ]
then
    echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm -rf ${OUTDIR}/rootfs
fi

# Create necessary base directories
echo "Creating base directories for root filesystem"
mkdir -p rootfs && cd rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log
mkdir -p home/conf

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
else
    cd busybox
fi

# Make and install busybox
echo "Making and installing busybox"
#make distclean
#make defconfig
#make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
#make CONFIG_PREFIX=bin ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}

echo "Library dependencies"
${CROSS_COMPILE}readelf -a busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a busybox | grep "Shared library"

# Add library dependencies to rootfs
echo "Copying library dependencies to rootfs"
SYSROOT=$(${CROSS_COMPILE}gcc --print-sysroot)
cd "$OUTDIR/rootfs"
cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 lib/ld-linux-aarch64.so.1
cp ${SYSROOT}/lib64/libm.so.6 lib64/libm.so.6
cp ${SYSROOT}/lib64/libresolv.so.2 lib64/libresolv.so.2
cp ${SYSROOT}/lib64/libc.so.6 lib64/libc.so.6

# Make device nodes
echo "Creating device nodes"
if [ -e dev/null ]
then
    sudo rm dev/null
fi
if [ -e dev/console ]
then
    sudo rm dev/console
fi

sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

# Clean and build the writer utility
echo "Cleaning and building writer utility"
cd "$FINDER_APP_DIR"
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory
# on the target rootfs
echo "Copying finder scripts/executables"
cp writer ${OUTDIR}/rootfs/home/writer
cp finder.sh ${OUTDIR}/rootfs/home/finder.sh
cp ../conf/username.txt ${OUTDIR}/rootfs/home/conf/username.txt
cp ../conf/assignment.txt ${OUTDIR}/rootfs/home/conf/assignment.txt
cp finder-test.sh ${OUTDIR}/rootfs/home/finder-test.sh
cp autorun-qemu.sh ${OUTDIR}/rootfs/home/autorun-qemu.sh

# Chown the root directory
cd "$OUTDIR"
sudo chown root:root rootfs

# Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd "$OUTDIR"
gzip -f initramfs.cpio
