# Forwarding Makefile to allow building from repository root.

.DEFAULT_GOAL := build

.PHONY: all build profile-build net install clean strip analyze help objclean profileclean config-sanity
all build profile-build net install clean strip analyze help objclean profileclean config-sanity:
	$(MAKE) -C src $@

# Fallback rule to forward any other single target to the engine Makefile.
%:
	$(MAKE) -C src $@
