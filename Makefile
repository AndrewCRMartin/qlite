DESTDIR = /usr/local/apps/qlite/bin
MANDIR = /usr/local/apps/qlite/man
SPOOLDIR = /usr/local/spool/qlite

# Compile-time options for programs
# -DROOT_ONLY          makes qlsuspend only useable by root
# -DFILE_BASED_LOCKING makes locking use a file rather than qllockd
# -DREQUIRE_ACK        makes qllockd require 'ACK' or 'QUIT' rather
#                      than just mopping up any remaining characters
# QLOPTS = -DROOT_ONLY -DFILE_BASED_LOCKING -DREQUIRE_ACK
QLOPTS = -DROOT_ONLY

# For Linux...
CC = cc -g -pedantic -Wall -DDEF_SPOOLDIR=\"$(SPOOLDIR)\"
# For IRIX...
# CC = cc -fullwarn DEF_SPOOLDIR=\"$(SPOOLDIR)\"
LINK = cc 
EXEFILES = qlsubmit qlrun qllist qlshutdown qlsuspend qllockd 
OFILES1 = qlsubmit.o qlutil.o qlclient.o
OFILES2 = qlrun.o qlutil.o qlclient.o
OFILES3 = qllist.o qlutil.o
OFILES4 = qlshutdown.o qlutil.o
OFILES5 = qlsuspend.o qlutil.o
OFILES6 = qllockd.o qlutil.o
INSTALLOPT = -g root -o root

all : $(EXEFILES)

qlsubmit : $(OFILES1)
	$(LINK) -o $@ $(OFILES1)

qlrun : $(OFILES2)
	$(LINK) -o $@ $(OFILES2)

qllist : $(OFILES3)
	$(LINK) -o $@ $(OFILES3)

qlshutdown : $(OFILES4)
	$(LINK) -o $@ $(OFILES4)

qlsuspend : $(OFILES5)
	$(LINK) -o $@ $(OFILES5)

qllockd : $(OFILES6)
	$(LINK) -o $@ $(OFILES6)

.c.o :
	$(CC) $(QLOPTS) -c $<

clean :
	\rm -f qlsubmit.o qlrun.o qlutil.o qllist.o qlshutdown.o \
               qllockd.o qlclient.o qlsuspend.o
distrib : clean
	\rm $(EXEFILES)

install :
	install -d $(INSTALLOPT) -m 755 $(DESTDIR)
	install $(INSTALLOPT) -m 4555 qlsubmit   $(DESTDIR)	
	install $(INSTALLOPT) -m 555  qlshutdown $(DESTDIR)	
	install $(INSTALLOPT) -m 555  qlrun      $(DESTDIR)
	install $(INSTALLOPT) -m 555  qllist     $(DESTDIR)
	install $(INSTALLOPT) -m 555  qllockd    $(DESTDIR)
	install $(INSTALLOPT) -m 4555 qlsuspend  $(DESTDIR)
	install -d $(INSTALLOPT) -m 755 $(SPOOLDIR)
	install -d $(INSTALLOPT) -m 755 $(MANDIR)
	install -m 644 doc/*.1          $(MANDIR)
