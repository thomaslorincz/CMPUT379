# ------------------------------------------------------------
# Thomas Lorincz - CMPUT 379 A1
#
# Usage: make // compile programs
#        make tar // create a 'tar.gz' archive of 'allFiles'
#        make clean // remove unneeded files
# ------------------------------------------------------------

target = submit
allFiles = Makefile a2sdn.cpp controller.cpp controller.h switch.cpp switch.h util.cpp util.h

compile:
	g++ -std=c++11 -Wall a2sdn.cpp controller.cpp controller.h switch.cpp switch.h util.cpp util.h -o a2sdn

tar:
	tar -cvf $(target).tar $(allFiles)
	gzip $(target).tar

clean:
	rm -f *~ out.* *.o