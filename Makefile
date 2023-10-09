#!/usr/bin/make -f
# Waf Makefile wrapper
WAF_HOME=/workspaces/codespaces-blank

all:
	@/workspaces/codespaces-blank/waf build

all-debug:
	@/workspaces/codespaces-blank/waf -v build

all-progress:
	@/workspaces/codespaces-blank/waf -p build

install:
	if test -n "$(DESTDIR)"; then \
	    /workspaces/codespaces-blank/waf install --yes --destdir="$(DESTDIR)" ; \
	else \
	    /workspaces/codespaces-blank/waf install --yes ; \
	fi;

uninstall:
	@if test -n "$(DESTDIR)"; then \
	    /workspaces/codespaces-blank/waf uninstall --destdir="$(DESTDIR)" ; \
	else \
	    /workspaces/codespaces-blank/waf uninstall ; \
	fi;
 
test: all
	@for i in test/test*.js; do \
		echo -n "$$i: "; \
		build/default/node $$i && echo pass || echo fail; \
	done 

clean:
	@/workspaces/codespaces-blank/waf clean

distclean:
	@/workspaces/codespaces-blank/waf distclean
	@-rm -rf _build_
	@-rm -f Makefile
	@-rm -f *.pyc

check:
	@/workspaces/codespaces-blank/waf check

dist:
	@/workspaces/codespaces-blank/waf dist

.PHONY: clean dist distclean check uninstall install all test

