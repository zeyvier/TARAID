#!/bin/bash

dev_cnt=$0
rd_lev=$1
ck_sz=$2

devlist=""

for((i=0 ;i<dev_cnt ;i++));
do
    devlist=${devlist}" /dev/nvme${i}n1";
done

sudo mdadm --create --verbose /dev/md0 --level=${rd_lev} --chunk=${ck_sz}K --raid-devices=${dev_cnt} ${devlist}

sudo mkfs.ext4 /dev/md0

sudo mount /dev/md0 /home/zeyvier/r0

echo "raid level:${rd_lev}"
echo "chunk size:${ck_sz}"