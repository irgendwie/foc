PKGDIR         ?= ../..
L4DIR          ?= $(PKGDIR)/../..

TARGET          = virtio-trusty-proxy
REQUIRES_LIBS   = l4virtio libpthread
PRIVATE_INCDIR += $(PKGDIR)/server/include
SRC_CC          = main.cc

include $(L4DIR)/mk/prog.mk
