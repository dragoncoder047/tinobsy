.PHONY: test64 builtest64 valgrind64 clean test32 buildtest32 valgrind32 deps show checkleaks

test: buildtest64 valgrind64 buildtest32 valgrind32 clean

buildtest64:
	g++ --std=c++11 tinobsy_test.cpp -g -o ttest64

valgrind64: buildtest64
	valgrind --track-origins=yes ./ttest64 > test/out64.txt 2> test/valgrind64.txt

buildtest32:
	g++ --std=c++11 -m32 tinobsy_test.cpp -g -o ttest32

valgrind32: buildtest32
	valgrind --track-origins=yes ./ttest32 > test/out32.txt 2> test/valgrind32.txt

clean:
	rm -f ttest64
	rm -f ttest32
	rm -f vgcore.*

deps:
	sudo dpkg --add-architecture i386
	sudo apt-get update
	sudo apt-get install valgrind --yes
	sudo apt-get install gcc-multilib --yes
	sudo apt-get install g++-multilib --yes
	sudo apt-get install libgcc-s1:i386 --yes
	sudo apt-get install libc6-dbg:i386 --yes

show:
	cat test/out64.txt
	cat test/valgrind64.txt
	cat test/out32.txt
	cat test/valgrind32.txt

checkleaks:
	cat test/valgrind64.txt | grep "no leaks are possible" >/dev/null
	cat test/valgrind32.txt | grep "no leaks are possible" >/dev/null
