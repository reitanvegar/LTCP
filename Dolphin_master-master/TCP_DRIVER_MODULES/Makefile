#Makefile to build rdma_daemon and local_tcp_protocol 
SHELL=/bin/bash
obj-m +=  rdma/rdma_daemon.o 

log-objs := rdma/rdma_daemon.o

# install the dolphin drivers with --get-tarball and run sudo ./build_tarball.sh in the DIS dir.
SRC=$(shell pwd)/DIS/src

KDIR=/lib/modules/$(shell uname -r)/build

DIS_SYMBOLS=$(SRC)/IRM_GX/drv/src/LINUX/Module.symvers
KOSIF_SYMBOLS=$(SRC)/COMMON/osif/kernel/src/LINUX/Module.symvers

CPPFLAGS += -D_DIS_KERNEL_ -DOS_IS_LINUX -DCPU_ADDR_IS_64_BIT \
            -DLINUX -DUNIX -DCPU_ARCH_IS_X86_64 -DADAPTER_IS_PX -DMAP_USER_PAGES_SEND -DMAP_USER_PAGES_RECV #-DLTCP_DEBUG #-DPIO
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

                        
EXTRA_CFLAGS += ${CPPFLAGS}

all: 
	cp -f ${DIS_SYMBOLS} ./ || :
	cat ${KOSIF_SYMBOLS} >> ./Module.symvers || :
	make -C ${KDIR} EXTRA_CFLAGS="$(EXTRA_CFLAGS)" M=$(shell pwd) modules

IP_SRC=$(shell hostname -I | awk '{print $$1}')
#export IP_SRC

insert_module:
	/opt/DIS/sbin/disinfo -hostnames | while IFS= read -r line ; do\
		if [ $$line != $(IP_SRC) ]; then\
			NODE_ID_SRC=$(shell /opt/DIS/sbin/disinfo get-nodeid -hostname $(IP_SRC) -ad 0);\
			NODE_ID_DEST=4;\
			if [ $$NODE_ID_SRC = 4 ]; then\
				NODE_ID_DEST=8;\
			fi;\
			echo "NodeId src: $$NODE_ID_SRC";\
			echo "NodeId dest: $$NODE_ID_DEST";\
			IP_DEST="$$line";\
			echo "IP src: $(IP_SRC)";\
			echo "IP dest: $$IP_DEST";\
			insmod rdma/rdma_daemon.ko node_id=$$NODE_ID_SRC remote_node_id=$$NODE_ID_DEST src=$(IP_SRC) dest=$$IP_DEST;\
			break;\
		fi;\
	done;

clean_dis:
	$(shell rm -rf DIS.tar.gz DIS_install.log*)

build_dis: clean_dis
	$(shell sudo bash ../dolphin_installation/Dolphin_eXpressWare_drivers.sh --get-tarball && sudo tar -xvf DIS.tar.gz && cd DIS/ && sudo ./build_tarball.sh)


clean:
	make -C ${KDIR} M=$(shell pwd) clean
