# ------------------------------------------------------------
# Thomas Lorincz - CMPUT 379 A1
#
# Usage: make // compile programs
#        make tar // create a 'tar.gz' archive of 'allFiles'
#        make clean // remove unneeded files
# ------------------------------------------------------------

target = submit
allFiles = Makefile a3sdn.cpp controller.cpp controller.h switch.cpp switch.h util.cpp util.h report.pdf

compile:
	g++ -std=c++11 -Wall a3sdn.cpp controller.cpp controller.h switch.cpp switch.h util.cpp util.h -o a3sdn

tar:
	tar -cvf $(target).tar $(allFiles)
	gzip $(target).tar

clean:
	rm -f *~ out.* *.o