PKGDIR ?= ..
L4DIR  ?= $(PKGDIR)/../..

TEST_GROUP := virtio-trusty-proxy

REQUIRES_LIBS  := libpthread atkins
DEPENDS_PKGS   := atkins

NED_CFG := virtio-trusty-test.ned

REQUIRED_MODULES := virtio-trusty-proxy

PRIVATE_INCDIR += $(PKGDIR)/server/src

include $(L4DIR)/mk/test.mk
