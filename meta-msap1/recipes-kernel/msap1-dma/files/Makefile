# One module, three objects: the shared transport core plus one small
# personality file per acquisition device.
obj-m += msap1_dma.o
msap1_dma-objs := msap1_dma_core.o msap1_dma_meter.o msap1_dma_waveform.o

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean
