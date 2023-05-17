.PHONY: test builtest valgrind clean

buildtest:
	gcc tinobsy_test.c -g -o ttest

valgrind: buildtest
	valgrind --track-origins=yes ./ttest > test/out.txt 2> test/valgrind.txt

clean:
	rm -f ttest

test: buildtest valgrind clean
