# ------------------------------------------------------------
# Thomas Lorincz - CMPUT 379 A1
#
# Usage: make // compile programs
#        make tar // create a 'tar.gz' archive of 'allFiles'
#        make clean // remove unneeded files
# ------------------------------------------------------------

target = submit
allFiles = Makefile a1mon.cpp a1jobs.cpp report.pdf

compile:
	g++ -std=c++11 -Wall a1jobs.cpp -o a1jobs
	g++ -std=c++11 -Wall a1mon.cpp -o a1mon

tar:
	tar -cvf $(target).tar $(allFiles)
	gzip $(target).tar

clean:
	rm -f *~ out.* *.o