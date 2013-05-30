package		:= systemd-cron
version		:= $(shell cat VERSION)

srcdir		:= $(CURDIR)/src
outdir		:= $(CURDIR)/out
distname	:= $(package)-$(version)
distdir		:= $(outdir)/$(distname)
tarball		:= $(outdir)/$(distname).tar.xz

prefix		:= /usr/local
bindir		:= $(prefix)/bin
confdir		:= $(prefix)/etc
datadir		:= $(prefix)/share
libdir		:= $(prefix)/lib
statedir	:= $(prefix)/var
mandir		:= $(datadir)/man
docdir		:= $(datadir)/doc/$(package)
unitdir		:= $(libdir)/systemd/system

RUNPARTS	:= /usr/bin/run-parts

SED_SERVICE := sed \
	-e "s|/usr/local/etc|$(confdir)|g" \
	-e "s|/usr/bin/run-parts|$(RUNPARTS)|g"

SED_MAN := sed \
	-e "s|/usr/local/etc|$(confdir)|g" \
	-e "s|unknown version|$(version)|g"

all: clean

	mkdir -p $(outdir)
	mkdir -p $(outdir)/man
	mkdir -p $(outdir)/units

	$(SED_MAN) $(srcdir)/man/systemd.cron.7 > $(outdir)/man/systemd.cron.7

	$(SED_SERVICE) $(srcdir)/units/cron-boot.service	> \
		$(outdir)/units/cron-boot.service
	$(SED_SERVICE) $(srcdir)/units/cron-hourly.service	> \
		$(outdir)/units/cron-hourly.service
	$(SED_SERVICE) $(srcdir)/units/cron-daily.service	> \
		$(outdir)/units/cron-daily.service
	$(SED_SERVICE) $(srcdir)/units/cron-weekly.service	> \
		$(outdir)/units/cron-weekly.service
	$(SED_SERVICE) $(srcdir)/units/cron-monthly.service	> \
		$(outdir)/units/cron-monthly.service

	ln -s $(srcdir)/units/cron.target $(outdir)/units/cron.target

	ln -s $(srcdir)/units/cron-boot.timer		\
		$(outdir)/units/cron-boot.timer
	ln -s $(srcdir)/units/cron-hourly.timer		\
		$(outdir)/units/cron-hourly.timer
	ln -s $(srcdir)/units/cron-daily.timer		\
		$(outdir)/units/cron-daily.timer
	ln -s $(srcdir)/units/cron-weekly.timer		\
		$(outdir)/units/cron-weekly.timer
	ln -s $(srcdir)/units/cron-monthly.timer	\
		$(outdir)/units/cron-monthly.timer

clean:

	rm -rf $(outdir)

dist: $(tarball)

install: all

	install -d $(DESTDIR)$(confdir)
	install -d $(DESTDIR)$(unitdir)
	install -d $(DESTDIR)$(mandir)
	install -d $(DESTDIR)$(mandir)/man7

	install -d $(DESTDIR)$(confdir)/cron.boot
	install -d $(DESTDIR)$(confdir)/cron.hourly
	install -d $(DESTDIR)$(confdir)/cron.daily
	install -d $(DESTDIR)$(confdir)/cron.weekly
	install -d $(DESTDIR)$(confdir)/cron.monthly

	install -m644 $(outdir)/man/systemd.cron.7 $(DESTDIR)$(mandir)/man7

	install -m644 $(outdir)/units/cron.target $(DESTDIR)$(unitdir)

	install -m644 $(outdir)/units/cron-boot.timer		$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-hourly.timer		$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-daily.timer		$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-weekly.timer		$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-monthly.timer	$(DESTDIR)$(unitdir)

	install -m644 $(outdir)/units/cron-boot.service		$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-hourly.service	$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-daily.service	$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-weekly.service	$(DESTDIR)$(unitdir)
	install -m644 $(outdir)/units/cron-monthly.service	$(DESTDIR)$(unitdir)

uninstall:

	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(confdir)/cron.boot
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(confdir)/cron.hourly
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(confdir)/cron.daily
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(confdir)/cron.weekly
	rmdir --ignore-fail-on-non-empty $(DESTDIR)$(confdir)/cron.monthly

	rm -f $(DESTDIR)$(unitdir)/cron.target

	rm -f $(DESTDIR)$(unitdir)/cron-boot.timer
	rm -f $(DESTDIR)$(unitdir)/cron-hourly.timer
	rm -f $(DESTDIR)$(unitdir)/cron-daily.timer
	rm -f $(DESTDIR)$(unitdir)/cron-weekly.timer
	rm -f $(DESTDIR)$(unitdir)/cron-monthly.timer

	rm -f $(DESTDIR)$(unitdir)/cron-boot.service
	rm -f $(DESTDIR)$(unitdir)/cron-hourly.service
	rm -f $(DESTDIR)$(unitdir)/cron-daily.service
	rm -f $(DESTDIR)$(unitdir)/cron-weekly.service
	rm -f $(DESTDIR)$(unitdir)/cron-monthly.service

$(tarball): $(distdir)

	cd $(distdir)/..; \
		tar -cJ --owner=root --group=root --file $(tarball) $(distname)

$(distdir):

	mkdir -p $(distdir)

	cp -a Makefile	$(distdir)
	cp -a LICENSE	$(distdir)
	cp -a README.md	$(distdir)
	cp -a VERSION	$(distdir)
	cp -a src		$(distdir)

.PHONY: all clean dist install uninstall

