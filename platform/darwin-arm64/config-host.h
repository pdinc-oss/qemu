/*
 * Autogenerated by the Meson build system.
 * Do not edit, your changes will be lost.
 */

#pragma once

#undef CONFIG_ACCEPT4

#undef CONFIG_AF_ALG

#undef CONFIG_AF_VSOCK

#undef CONFIG_AF_XDP

#undef CONFIG_ALIGNED_MALLOC

#define CONFIG_ANDROID

#define CONFIG_ARM_AES_BUILTIN

#define CONFIG_ASAN_IFACE_FIBER

#define CONFIG_ATOMIC128

#define CONFIG_ATOMIC64

#undef CONFIG_ATTR

#undef CONFIG_AUDIO_ALSA

#define CONFIG_AUDIO_COREAUDIO

#define CONFIG_AUDIO_DRIVERS "coreaudio", 

#undef CONFIG_AUDIO_DSOUND

#undef CONFIG_AUDIO_JACK

#undef CONFIG_AUDIO_OSS

#undef CONFIG_AUDIO_PA

#undef CONFIG_AUDIO_PIPEWIRE

#undef CONFIG_AUDIO_SDL

#undef CONFIG_AUDIO_SNDIO

#undef CONFIG_AVX2_OPT

#undef CONFIG_AVX512BW_OPT

#undef CONFIG_AVX512F_OPT

#define CONFIG_BDRV_RO_WHITELIST 

#define CONFIG_BDRV_RW_WHITELIST 

#undef CONFIG_BDRV_WHITELIST_TOOLS

#define CONFIG_BINDIR "rel_dir/bin"

#undef CONFIG_BLKIO

#undef CONFIG_BLKZONED

#undef CONFIG_BRLAPI

#define CONFIG_BSD

#undef CONFIG_CAPSTONE

#undef CONFIG_CFI

#undef CONFIG_CLOCK_ADJTIME

#undef CONFIG_CLOSE_RANGE

#define CONFIG_COCOA

#define CONFIG_COROUTINE_POOL

#undef CONFIG_CPUID_H

#undef CONFIG_CURL

#undef CONFIG_CURSES

#define CONFIG_DARWIN

#undef CONFIG_DBUS_DISPLAY

#undef CONFIG_DEBUG_GRAPH_LOCK

#undef CONFIG_DEBUG_MUTEX

#undef CONFIG_DEBUG_STACK_USAGE

#undef CONFIG_DEBUG_TCG

#undef CONFIG_DUP3

#undef CONFIG_EBPF

#undef CONFIG_EPOLL

#undef CONFIG_EPOLL_CREATE1

#undef CONFIG_EVENTFD

#undef CONFIG_FALLOCATE

#undef CONFIG_FALLOCATE_PUNCH_HOLE

#undef CONFIG_FALLOCATE_ZERO_RANGE

#undef CONFIG_FDATASYNC

#define CONFIG_FDT

#undef CONFIG_FIEMAP

#undef CONFIG_FUSE

#undef CONFIG_FUSE_LSEEK

#undef CONFIG_FUZZ

#undef CONFIG_GBM

#undef CONFIG_GCOV

#undef CONFIG_GCRYPT

#undef CONFIG_GETAUXVAL

#undef CONFIG_GETCPU

#undef CONFIG_GETRANDOM

#undef CONFIG_GETTID

#undef CONFIG_GIO

#undef CONFIG_GLUSTERFS

#undef CONFIG_GNUTLS

#undef CONFIG_GNUTLS_CRYPTO

#undef CONFIG_GTK

#undef CONFIG_GTK_CLIPBOARD

#define CONFIG_HEXAGON_IDEF_PARSER

#undef CONFIG_HOGWEED

#define CONFIG_HOST_DSOSUF ".dylib"

#undef CONFIG_INOTIFY

#undef CONFIG_INOTIFY1

#define CONFIG_INT128

#define CONFIG_INT128_TYPE

#define CONFIG_IOVEC

#undef CONFIG_KEYUTILS

#define CONFIG_KVM_TARGETS ""

#undef CONFIG_L2TPV3

#undef CONFIG_LIBATTR

#undef CONFIG_LIBCAP_NG

#undef CONFIG_LIBDAXCTL

#undef CONFIG_LIBDW

#undef CONFIG_LIBISCSI

#undef CONFIG_LIBNFS

#undef CONFIG_LIBPMEM

#undef CONFIG_LIBSSH

#undef CONFIG_LIBUDEV

#undef CONFIG_LINUX

#undef CONFIG_LINUX_AIO

#undef CONFIG_LINUX_IO_URING

#undef CONFIG_LINUX_MAGIC_H

#define CONFIG_LIVE_BLOCK_MIGRATION

#undef CONFIG_LZO

#define CONFIG_MADVISE

#undef CONFIG_MALLOC_TRIM

#undef CONFIG_MEMALIGN

#undef CONFIG_MEMBARRIER

#undef CONFIG_MEMFD

#define CONFIG_MODULES

#undef CONFIG_MODULE_UPGRADES

#undef CONFIG_MPATH

#undef CONFIG_NETMAP

#undef CONFIG_NETTLE

#undef CONFIG_NUMA

#undef CONFIG_OPENGL

#undef CONFIG_OPEN_BY_HANDLE

#define CONFIG_PIXMAN

#undef CONFIG_PLUGIN

#undef CONFIG_PNG

#define CONFIG_POSIX

#undef CONFIG_POSIX_FALLOCATE

#define CONFIG_POSIX_MADVISE

#define CONFIG_POSIX_MEMALIGN

#undef CONFIG_PPOLL

#undef CONFIG_PRCTL_PR_SET_TIMERSLACK

#define CONFIG_PREADV

#define CONFIG_PREFIX "rel_dir"

#undef CONFIG_PTHREAD_AFFINITY_NP

#undef CONFIG_PTHREAD_CONDATTR_SETCLOCK

#define CONFIG_PTHREAD_FCHDIR_NP

#define CONFIG_PTHREAD_SETNAME_NP_WO_TID

#undef CONFIG_PTHREAD_SETNAME_NP_W_TID

#undef CONFIG_PTHREAD_SET_NAME_NP

#define CONFIG_QEMU_CONFDIR "rel_dir/etc/qemu"

#define CONFIG_QEMU_DATADIR "rel_dir/share/qemu"

#define CONFIG_QEMU_DESKTOPDIR "rel_dir/share/applications"

#define CONFIG_QEMU_FIRMWAREPATH "rel_dir/share/qemu-firmware", 

#define CONFIG_QEMU_HELPERDIR "rel_dir/libexec"

#define CONFIG_QEMU_ICONDIR "rel_dir/share/icons"

#define CONFIG_QEMU_LOCALEDIR "rel_dir/share/locale"

#define CONFIG_QEMU_LOCALSTATEDIR "rel_dir/var"

#define CONFIG_QEMU_MODDIR "rel_dir/lib/qemu"

#undef CONFIG_QEMU_PRIVATE_XTS

#define CONFIG_QOM_CAST_DEBUG

#undef CONFIG_RBD

#undef CONFIG_RDMA

#define CONFIG_RELOCATABLE

#undef CONFIG_REPLICATION

#undef CONFIG_RTNETLINK

#undef CONFIG_SAFESTACK

#undef CONFIG_SCHED_GETCPU

#undef CONFIG_SDL

#undef CONFIG_SDL_IMAGE

#undef CONFIG_SECCOMP

#undef CONFIG_SECRET_KEYRING

#undef CONFIG_SELINUX

#define CONFIG_SENDFILE

#undef CONFIG_SETNS

#undef CONFIG_SIGNALFD

#undef CONFIG_SLIRP

#undef CONFIG_SNAPPY

#undef CONFIG_SOLARIS

#undef CONFIG_SPICE

#undef CONFIG_SPICE_PROTOCOL

#undef CONFIG_SPLICE

#define CONFIG_STAMP _10e1e2d32cef3cb12d922a8407d82e1a9eaa7a1c

#undef CONFIG_STATX

#undef CONFIG_STATX_MNT_ID

#undef CONFIG_SYNCFS

#undef CONFIG_SYNC_FILE_RANGE

#define CONFIG_SYSCONFDIR "rel_dir/etc"

#undef CONFIG_SYSMACROS

#undef CONFIG_TASN1

#define CONFIG_TCG 1

#undef CONFIG_TCG_INTERPRETER

#undef CONFIG_TIMERFD

#define CONFIG_TLS_PRIORITY "NORMAL"

#undef CONFIG_TPM

#define CONFIG_TRACE_FILE "trace"

#define CONFIG_TRACE_LOG

#undef CONFIG_TSAN

#undef CONFIG_USBFS

#undef CONFIG_USB_LIBUSB

#undef CONFIG_VALGRIND_H

#define CONFIG_VALLOC

#undef CONFIG_VDE

#undef CONFIG_VDUSE_BLK_EXPORT

#undef CONFIG_VHOST

#undef CONFIG_VHOST_CRYPTO

#undef CONFIG_VHOST_KERNEL

#undef CONFIG_VHOST_NET

#undef CONFIG_VHOST_NET_USER

#undef CONFIG_VHOST_NET_VDPA

#undef CONFIG_VHOST_USER

#undef CONFIG_VHOST_USER_BLK_SERVER

#undef CONFIG_VHOST_VDPA

#undef CONFIG_VIRTFS

#define CONFIG_VMNET

#define CONFIG_VNC

#undef CONFIG_VNC_JPEG

#undef CONFIG_VNC_SASL

#undef CONFIG_VTE

#undef CONFIG_WIN32

#undef CONFIG_X11

#undef CONFIG_XEN_BACKEND

#undef CONFIG_XKBCOMMON

#undef CONFIG_ZSTD

#undef HAVE_BLK_ZONE_REP_CAPACITY

#undef HAVE_BROKEN_SIZE_MAX

#undef HAVE_BTRFS_H

#undef HAVE_COPY_FILE_RANGE

#undef HAVE_DRM_H

#undef HAVE_FSXATTR

#define HAVE_GETIFADDRS

#define HAVE_GETLOADAVG_FUNCTION

#undef HAVE_GLIB_WITH_SLICE_ALLOCATOR

#define HAVE_HOST_BLOCK_DEVICE

#undef HAVE_IPPROTO_MPTCP

#undef HAVE_MADVISE_WITHOUT_PROTOTYPE

#define HAVE_MLOCKALL

#define HAVE_OPENPTY

#define HAVE_OPTRESET

#undef HAVE_PTY_H

#undef HAVE_SIGEV_NOTIFY_THREAD_ID

#undef HAVE_STRCHRNUL

#undef HAVE_STRUCT_STAT_ST_ATIM

#define HAVE_SYSTEM_FUNCTION

#define HAVE_SYS_DISK_H

#define HAVE_SYS_IOCCOM_H

#undef HAVE_SYS_KCOV_H

#define HAVE_UTMPX

#undef HAVE_VSS_SDK

#define HOST_AARCH64 1

#define QEMU_VERSION "8.2.0"

#define QEMU_VERSION_MAJOR 8

#define QEMU_VERSION_MICRO 0

#define QEMU_VERSION_MINOR 2

