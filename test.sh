#!/bin/bash

echo "##########Start test##########\n"

success=0
fail=0
j=0

for i in `ls CSV`
do
	#echo $i
	file="CSV/$i"
	if [ -f $file ]; then
		echo "$file"
		./PAlogparser -q $file

		if [ $? -ne 0 ]; then
			#echo "test failed: $file ($?)"
			FAIL_FILES[$j]=$i
			j=`expr $j + 1`
			fail=`expr $fail + 1`
		else
			success=`expr $success + 1`
		fi
	#else
		#echo "not file: $file"
	fi

done

echo "success: $success fail: $fail"
echo "fail files: ${FAIL_FILES[*]}"

echo "##########End test##########\n"