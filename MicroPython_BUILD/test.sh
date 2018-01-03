#!/bin/bash

. "build_func.sh"


FLASH_SIZE=4096
MP_SHOW_PROGRESS=no

get_arguments "$@"

echo "Result: $?"

echo "  Progress: [${MP_SHOW_PROGRESS}]"
echo "Flash size: [${FLASH_SIZE}]"
echo " -j option: [${J_OPTION}]"

echo "Params:     [${POSITIONAL_ARGS[@]}]"

echo "Number of positional arguments = ${#POSITIONAL_ARGS[@]}"
idx=1
for i in "${POSITIONAL_ARGS[@]}"
do
   echo "Arg at ${idx} = [${i}] [${POSITIONAL_ARGS[$(( idx - 1 ))]}]"
   idx=$(( idx + 1 ))
done


