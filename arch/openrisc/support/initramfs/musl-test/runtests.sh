for i in `find . -iname \*exe`; do
    echo $i
    ./common/runtest.exe $i
done