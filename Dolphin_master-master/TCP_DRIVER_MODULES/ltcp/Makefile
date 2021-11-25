#Makefile to build rdma_daemon and local_tcp_protocol 


obj-m +=  local_tcp_protocol.o 

log-objs := local_tcp_protocol.o

# install the dolphin drivers with --get-tarball and run sudo ./build_tarball.sh in the DIS dir.
SRC=$(shell pwd)/../DIS/src

KDIR=/lib/modules/$(shell uname -r)/build

DIS_SYMBOLS=$(SRC)/IRM_GX/drv/src/LINUX/Module.symvers
KOSIF_SYMBOLS=$(SRC)/COMMON/osif/kernel/src/LINUX/Module.symvers

CPPFLAGS += -D_DIS_KERNEL_ -DOS_IS_LINUX -DCPU_ADDR_IS_64_BIT \
            -DLINUX -DUNIX -DCPU_ARCH_IS_X86_64 -DADAPTER_IS_PX \
            -DLINK_IS_ETHERNET -DLTCP_DEBUG -DMAP_USER_PAGES_SEND -DMAP_USER_PAGES_RECV#-DUSE_DMA_WRITE
# Use MAP_USER_PAGES_SEND to map user space pages on the send side with get user space pages
# This requires map user pages function



# All modules share the same compilation flags at the moment.
EXTRA_CFLAGS +=  .                               \
                -I$(SRC)/include                   \
                -I$(SRC)/include/dis               \
                -I$(SRC)/COMMON/osif/kernel               \
                -I$(SRC)/COMMON/osif/kernel/include       \
                -I$(SRC)/COMMON/osif/kernel/include/LINUX \
                -I$(SRC)/IRM_GX/drv/src                   \
                -I$(SRC)/IRM_GX/drv/include               \
                -I$(SRC)/IRM_GX/drv/src/LINUX             \
                -I$(SRC)/IRM_GX/drv/src/LINUX/os          \
                -I$(SRC)/GX/drv/src/plx/dma          \
                -I$(SRC)/GX/drv/src/plx/ntb          \
                -I$(SRC)/GX/drv/src/plx/ntb/LINUX          \
                -I$(SRC)/GX/drv/src/plx/dma/LINUX          \
                -I$(SRC)/GX/drv/src/plx/dma/LINUX/os          \
                -I$(SRC)/GX/drv/src            \
                -Iltcp            \
                
                # -I/usr/src/linux-headers-$(uname -r)/arch/x86/mm/tlb\
                        
EXTRA_CFLAGS += ${CPPFLAGS}

all:
	cp -f ${DIS_SYMBOLS} ./ || :
	cat ${KOSIF_SYMBOLS} >> ./Module.symvers || :
	make -C ${KDIR} EXTRA_CFLAGS="$(EXTRA_CFLAGS)" M=$(shell pwd) modules

clean:
	make -C ${KDIR} M=$(shell pwd) clean

