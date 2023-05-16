.PHONY: test builtest testtest

buildtest:
	gcc tinobsy_test.c -o ttest

testtest: buildtest
	valgrind ./ttest > test/out.txt 2> test/valgrind.txt

test: testtest
	rm ttest
