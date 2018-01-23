man1 := $(wildcard $(srcdir)/man/*.1)
man5 := $(wildcard $(srcdir)/man/*.5)
man8 := $(wildcard $(srcdir)/man/*.8)

install::
	$(INSTALL_DIR) $(mandir)/man1
	$(INSTALL_DIR) $(mandir)/man5
	$(INSTALL_DIR) $(mandir)/man8
	$(INSTALL_DATA) $(man1) $(mandir)/man1
	$(INSTALL_DATA) $(man5) $(mandir)/man5
	$(INSTALL_DATA) $(man8) $(mandir)/man8

lang-all  := $(wildcard $(srcdir)/man/*/*.[0-9])
lang-all  := $(patsubst $(srcdir)/man/%,%,$(lang-all))
lang-man1 := $(filter %.1,$(lang-all))
lang-man5 := $(filter %.5,$(lang-all))
lang-man8 := $(filter %.8,$(lang-all))
langs-1   := $(patsubst %/,inst1-%,$(sort $(dir $(lang-man1))))
langs-5   := $(patsubst %/,inst5-%,$(sort $(dir $(lang-man5))))
langs-8   := $(patsubst %/,inst8-%,$(sort $(dir $(lang-man8))))

pages-1   = $(patsubst %,$(srcdir)/man/%,$(filter $*/%,$(lang-man1)))
pages-5   = $(patsubst %,$(srcdir)/man/%,$(filter $*/%,$(lang-man5)))
pages-8   = $(patsubst %,$(srcdir)/man/%,$(filter $*/%,$(lang-man8)))

install:: $(langs-1) $(langs-5) $(langs-8)

inst1-%:
	$(INSTALL_DIR) $(mandir)/$*/man1
	$(INSTALL_DATA) $(pages-1) $(mandir)/$*/man1

inst5-%:
	$(INSTALL_DIR) $(mandir)/$*/man5
	$(INSTALL_DATA) $(pages-5) $(mandir)/$*/man5

inst8-%:
	$(INSTALL_DIR) $(mandir)/$*/man8
	$(INSTALL_DATA) $(pages-8) $(mandir)/$*/man8
