
FILES-frequencies := \
	$(srcdir)/frequencies/Index.map \
	$(wildcard $(srcdir)/frequencies/*.list)

install:: $(FILES-frequencies)
	$(INSTALL_DIR) $(datadir)
	$(INSTALL_DATA) $(FILES-frequencies) $(datadir)

