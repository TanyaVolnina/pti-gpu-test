#!/bin/bash
BOM_DIR=$1
#echo BOM_DIR $BOM_DIR
TGT_FILE=$4
#echo TGT_FILE $TGT_FILE
TGT_BOM=$2
#echo TGT_BOM $TGT_BOM
BRK_TOKEN=$3
#echo BRK_TOKEN $BRK_TOKEN


[[ -d ${BOM_DIR} ]] || mkdir ${BOM_DIR}
echo ALL $@

STG1=${TGT_FILE##*/${BRK_TOKEN}/}
STRIPPED_NAME=${STG1#$BRK_TOKEN/}
echo STG1= ${STG1} STRIPPED_NAME ${STRIPPED_NAME}

echo "DEL"/${STRIPPED_NAME}$':'"INS"/${STRIPPED_NAME}$':'$(cksum ${TGT_FILE}| cut -d ' ' -f 1)$':'ONE$':'$':'INT$':'$(stat -c '%a' ${TGT_FILE}) >> ${TGT_BOM}
