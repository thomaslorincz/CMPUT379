# ------------------------------------------------------------
# Thomas Lorincz - CMPUT 379 A1
#
# Usage: make // compile programs
#        make tar // create a 'tar.gz' archive of 'allFiles'
#        make clean // remove unneeded files
# ------------------------------------------------------------

target = submit
allFiles = Makefile a4tasks.cpp

compile:
	g++ -std=c++11 -Wall a4tasks.cpp -pthread -o a4tasks

tar:
	tar -cvf $(target).tar $(allFiles)
	gzip $(target).tar

clean:
	rm -f *~ out.* *.o