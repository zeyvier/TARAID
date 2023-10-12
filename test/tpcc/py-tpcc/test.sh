durable_time=20
test_db="sqlite"
warehouses=4

cd ./pytpcc

cur_wh_str=$(cat ./db_file/warehouse.txt)
cur_wh_int=$((cur_wh_str))

echo "cur wh $cur_wh_int"

if [ $cur_wh_int -ne $warehouses ]; then
    sudo python2 tpcc.py --reset --config=sqlite.config  \
        --warehouses $warehouses \
        --duration $durable_time \
        $test_db
    echo $warehouses > ./db_file/warehouse.txt
else
    sudo python2 tpcc.py --no-load --config=sqlite.config  \
        --warehouses $warehouses \
        --duration $durable_time \
        $test_db
fi


  
